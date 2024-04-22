using Avalonia.Controls;
using FFmpeg.AutoGen;
using System;
using System.IO;
using System.Runtime.InteropServices;
using Avalonia.Threading;
using Avalonia.Media.Imaging;
using Avalonia;
using Avalonia.Platform;
using System.Text.Json.Serialization.Metadata;

namespace Storio;

public partial class MainWindow : Window
{
    public MainWindow()
    {
        ffmpeg.RootPath = "/Users/shanemorrison/lib";
        InitializeComponent();
        video_decode_example("/Users/shanemorrison/Storio/msw.avi");
    }


    unsafe void video_decode_example(string filename)
    {
        int result = Native.AddTowNumbers(20, 15);

        AVFrame* frame = null;
        AVPacket* pkt = null;

        frame = ffmpeg.av_frame_alloc();

        AVFormatContext* formatContext;
        ffmpeg.avformat_open_input(&formatContext, filename, null, null);
        ffmpeg.avformat_find_stream_info(formatContext, null);

        // Iterate over all streams and find the video stream
        AVStream* videoStream = null;
        for (var i = 0; i < formatContext->nb_streams; i++)
        {
            if (formatContext->streams[i]->codecpar->codec_type == AVMediaType.AVMEDIA_TYPE_VIDEO)
            {
                videoStream = formatContext->streams[i];
                break;
            }
        }

        if (videoStream == null)
        {
            Console.WriteLine("No video stream found in file.");
            return;
        }
        // Retrieve codec
        var codec = ffmpeg.avcodec_find_decoder(videoStream->codecpar->codec_id);
        if (codec == null)
        {
            Console.WriteLine("Codec not found.");
            ffmpeg.avformat_close_input(&formatContext);
            return;
        }

        // Allocate codec context
        var codecContext = ffmpeg.avcodec_alloc_context3(codec);
        ffmpeg.avcodec_parameters_to_context(codecContext, videoStream->codecpar);
        ffmpeg.avcodec_open2(codecContext, codec, null);

        // Allocate frame and packet
        frame = ffmpeg.av_frame_alloc();
        var packet = ffmpeg.av_packet_alloc();

        bool cont = true;
        int frames = 0;
        // Frame decoding loop
        while (cont && ffmpeg.av_read_frame(formatContext, packet) >= 0)
        {
            if (packet->stream_index == videoStream->index)
            {
                // Send the packet to the decoder
                ffmpeg.avcodec_send_packet(codecContext, packet);

                // Receive frame from the decoder
                while (cont && ffmpeg.avcodec_receive_frame(codecContext, frame) == 0)
                {
                    ProcessFrame(frame, codecContext);
                    ffmpeg.av_frame_unref(frame);
                    if (frames++ == 100)
                    cont = false;
                }
            }
            ffmpeg.av_packet_unref(packet);
        }

        // Clean up
        ffmpeg.av_frame_free(&frame);
        ffmpeg.av_packet_free(&packet);
        ffmpeg.avcodec_free_context(&codecContext);
        ffmpeg.avformat_close_input(&formatContext);
    }

    private unsafe void ProcessFrame(AVFrame* frame, AVCodecContext* codecContext)
    {
        int dstWidth = codecContext->width;
        int dstHeight = codecContext->height;
        AVPixelFormat dstPixFmt = AVPixelFormat.AV_PIX_FMT_BGRA; // BGRA is compatible with Avalonia

        // Allocate destination frame
        var convertedFrameBufferSize = ffmpeg.av_image_get_buffer_size(dstPixFmt,
            dstWidth,
            dstHeight,
            1);
        IntPtr iptr = Marshal.AllocHGlobal(convertedFrameBufferSize);        
        byte *bufferPtr = (byte *)iptr.ToPointer();
        var dataPtr = new byte_ptrArray4();
        var linesizePtr = new int_array4();
        ffmpeg.av_image_fill_arrays(ref dataPtr, ref linesizePtr, bufferPtr, dstPixFmt, dstWidth, dstHeight, 1);

        // Setup conversion context
        var swsContext = ffmpeg.sws_getContext(
            codecContext->width, codecContext->height, codecContext->pix_fmt,
            dstWidth, dstHeight, dstPixFmt,
            ffmpeg.SWS_BICUBIC, null, null, null);

        // Perform scaling (conversion)
        ffmpeg.sws_scale(swsContext, frame->data, frame->linesize, 0, codecContext->height, dataPtr, linesizePtr);

        // Create Avalonia bitmap and update UI
        Dispatcher.UIThread.InvokeAsync(() =>
        {
            var bitmap = new WriteableBitmap(new PixelSize(dstWidth, dstHeight), new Vector(96, 96), PixelFormat.Bgra8888, AlphaFormat.Opaque);
            using (var fb = bitmap.Lock())
            {
                for (int y = 0; y < dstHeight; y++)
                {
                    byte* src = dataPtr[0] + y * linesizePtr[0];
                    byte* dst = (byte*)fb.Address + y * fb.RowBytes;
                    Buffer.MemoryCopy(src, dst, fb.RowBytes, linesizePtr[0]);
                }
            }
            Marshal.FreeHGlobal(iptr);
            VideoFrameImage.Source = bitmap;            
        });

        // Cleanup
        //ffmpeg.av_frame_free(&convertedFrame);
        //ffmpeg.av_freep(&bufferPtr);
        ffmpeg.sws_freeContext(swsContext);
    }
}

public static class Native
{
    [DllImport("bu")]
    public static extern int AddTowNumbers(int a, int b);

}