#define FILE_OFFSET_BITS 64
#include <string>
#include <vector>
#include <chrono>
#include <stdio.h>

extern "C" {
#include <libavutil/opt.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/common.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libavutil/samplefmt.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

class Frame;


int FFMPEGioRead(void* opaque, uint8_t* buf, int bufSize)
{
    FILE* file = reinterpret_cast<FILE*>(opaque);
    size_t bytesRead = fread(buf, 1, bufSize, file);
    return bytesRead;
}

int64_t FFMPEGioSeek(void* opaque, int64_t pos, int whence)
{
    FILE* file = reinterpret_cast<FILE*>(opaque);
    if (whence == AVSEEK_SIZE)
    {
        long long curpos = ftello(file);
        fseeko(file, 0, SEEK_END);
        long long size = ftello(file);
        fseeko(file, curpos, SEEK_SET);

        return size;
    }
    fseeko(file, pos, whence);
    return pos;
}

// Simplify YUV pixel format checks by using the non-jpeg
// versions of ffmpeg pixel formats.
static AVPixelFormat TranslateFFmpegPixelFormat(AVPixelFormat format)
{
    if (format == AV_PIX_FMT_YUVJ420P) return AV_PIX_FMT_YUV420P;
    if (format == AV_PIX_FMT_YUVJ422P) return AV_PIX_FMT_YUV422P;

    return format;
}

class Frame
{
public:
    std::vector<uint8_t> data[4];
};

class Decoder
{
    std::string m_filename;
    SwsContext* swsContext = nullptr;
    int linesizes[4] = { 0,0,0,0 };
    int heights[4] = { 0,0,0,0 };
    AVFormatContext* format_context;
    uint32_t streamIdx = -1;
    AVCodecContext* pCodecContext = nullptr;
    double m_fps;    

public:
    Decoder() {}

    void Open(const std::string& filename)
    {
        m_filename = filename;
        Initialize();
    }

    void Initialize()
    {
        const size_t ioBufferSize(8 * 1024 * 1024);
        uint8_t* ioBuffer = (uint8_t*)av_malloc(ioBufferSize + AV_INPUT_BUFFER_PADDING_SIZE);

        if (ioBuffer)
        {
            memset(ioBuffer, 0, ioBufferSize + AV_INPUT_BUFFER_PADDING_SIZE);
        }
        format_context = avformat_alloc_context();

        FILE* f = fopen(m_filename.c_str(), "rb");
        if (!f) {
            fprintf(stderr, "Could not open %s\n", m_filename.c_str());
            exit(1);
        }
        AVIOContext* ioCtx = avio_alloc_context(ioBuffer, ioBufferSize, 0, f, FFMPEGioRead, nullptr, FFMPEGioSeek);
        // Setup the format context for custom I/O
        format_context->pb = ioCtx;
        format_context->flags |= AVFMT_FLAG_CUSTOM_IO | AVFMT_NOFILE;

        // Read data to probe

        uint64_t probeReadSize = 2048;
        fseeko(f, 0L, SEEK_END);
        long long sz = ftello(f);
        fseeko(f, 0L, SEEK_SET);
        size_t bytesRead = fread(ioBuffer, 1, probeReadSize, f);
        fseeko(f, 0L, SEEK_SET);
        int avStatus(0);

        bool failed = false;
        // Determine the input-format:
        if (bytesRead == probeReadSize)
        {
            // Set the ProbeData-structure for av_probe_input_format:
            AVProbeData probeData = { 0 };
            probeData.buf = ioBuffer;
            probeData.buf_size = probeReadSize;
            probeData.filename = "";
            probeData.mime_type = nullptr;

            format_context->iformat = av_probe_input_format(&probeData, 1);
            if (format_context->iformat == nullptr)
            {
                failed = true;
            }
            else
            {
                avStatus = avformat_open_input(&format_context, nullptr, nullptr, nullptr);
                // note, when avformat_open_input fails, it frees the formatContext.
            }
        }

        format_context->pb = ioCtx;
        format_context->flags |= AVFMT_FLAG_CUSTOM_IO | AVFMT_NOFILE;

        avformat_find_stream_info(format_context, nullptr);
        // Find the stream with the data we want.
        bool streamFound = false;
        uint32_t videoCnt = 0;

        for (uint32_t i = 0; i < format_context->nb_streams; ++i)
        {
            if (format_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
            {
                ++videoCnt;

                streamIdx = i;
                streamFound = true;
            }
        }
        AVStream* pStream = format_context->streams[streamIdx];

        int64_t frameCnt = pStream->nb_frames;
        m_fps = (pStream->r_frame_rate.num != 0 && pStream->r_frame_rate.den != 0) ?
            (double)pStream->r_frame_rate.num / (double)pStream->r_frame_rate.den : 29.97;

        const AVCodec* pCodec = nullptr;
        {
            pCodec = avcodec_find_decoder(pStream->codecpar->codec_id);
            pCodecContext = pStream->codec;

            // FFmpeg resets bits_per_raw_sample field during resampling,
            // set it back to original value.
            if (pCodecContext->sample_fmt == AV_SAMPLE_FMT_S16)
                pCodecContext->bits_per_raw_sample = 16;
        }

        avcodec_open2(pCodecContext, pCodec, nullptr);
    }

    double DecodeFrame(Frame& outFrame)
    {
        int frameIdx = 0;
        bool haveFrame = false;
        double timeSeconds;
        while (!haveFrame)
        {
            AVPacket packet;
            av_init_packet(&packet);
            int avStatus = av_read_frame(format_context, &packet);
            if (packet.stream_index == streamIdx)
            {
                avStatus = avcodec_send_packet(pCodecContext, &packet);
                AVFrame* pFrame = av_frame_alloc();
                avStatus = avcodec_receive_frame(
                    pCodecContext, pFrame);

                if (avStatus == 0)
                {
                    timeSeconds = pFrame->coded_picture_number / m_fps;
                    Scale(outFrame, pFrame, pCodecContext);
                    haveFrame = true;
                }
                av_packet_unref(&packet);
                av_frame_free(&pFrame);
            }
        }
        return timeSeconds;
    }

    int Width() { return pCodecContext->width; }
    int Height() { return pCodecContext->height; }

    void Scale(Frame& f, AVFrame* pFrame, AVCodecContext* pCodecContext)
    {
        AVPixelFormat  pixelFormat = TranslateFFmpegPixelFormat(pCodecContext->pix_fmt);
        if (swsContext == nullptr)
        {
            swsContext =
                sws_getContext(
                    pCodecContext->width, pCodecContext->height, pCodecContext->pix_fmt,
                    pCodecContext->width, pCodecContext->height, pixelFormat,
                    SWS_BICUBIC, nullptr, nullptr, nullptr);

            uint8_t* tmp[4] = { 0,0,0,0 };
            av_image_alloc(
                tmp,
                linesizes,
                pCodecContext->width,
                pCodecContext->height,
                pixelFormat,
                1);
            av_freep(&tmp[0]);

            heights[0] = pCodecContext->height;

            if (pixelFormat == AV_PIX_FMT_YUV420P)
            {
                heights[1] = pCodecContext->height / 2;
                heights[2] = pCodecContext->height / 2;
                heights[3] = 0;
            }
            else if (pixelFormat == AV_PIX_FMT_YUV422P)
            {
                heights[1] = pCodecContext->height;
                heights[2] = pCodecContext->height;
                heights[3] = 0;
            }
            else if (pixelFormat == AV_PIX_FMT_YUV410P)
            {
                heights[1] = pCodecContext->height / 4;
                heights[2] = pCodecContext->height / 4;
                heights[3] = 0;
            }
        }

        uint8_t* destData[4] = { 0,0,0,0 };
        for (int i = 0; i < 4; ++i)
        {
            if (linesizes[i] == 0)
                break;
            f.data[i].resize(linesizes[i] * heights[0]);
            destData[i] = f.data[i].data();
        }


        // Scale, meaning put the data in the right dimensions and do the
        // color conversion as indicated by m_pSwsContext (which is the most expensive part).
        int sliceHeight = sws_scale(
            swsContext,
            pFrame->data,
            pFrame->linesize,
            0,
            pCodecContext->height,
            destData,
            linesizes);
    }
};