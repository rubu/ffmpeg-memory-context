#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <iostream>

extern "C"
{
#include <libavformat/avformat.h>
}

class MemoryIoContext
{
public:
    MemoryIoContext(size_t buffer_size = 4096, bool blocking = false) : buffer_size_(buffer_size),
        blocking_(blocking),
        buffer_(reinterpret_cast<unsigned char*>(av_malloc(buffer_size_)), av_free)
    {
    }

    std::unique_ptr<AVIOContext, decltype(&av_free)> CreateAvIoContext()
    {
        std::unique_ptr<AVIOContext, decltype(&av_free)> av_io_context(avio_alloc_context(buffer_.get(), buffer_size_, 0, this, &MemoryIoContext::ReadPacket, NULL, NULL), &av_free);
        return av_io_context;
    }

    unsigned char *GetBuffer() const
    {
        return buffer_.get();
    }

    size_t GetBufferSize() const
    {
        return buffer_size_;
    }

    bool IsBlocking() const
    {
        return blocking_;
    }

    static int ReadPacket(void *opaque, uint8_t *buffer, int buffer_size)
    {
        auto *memory_io_context = static_cast<MemoryIoContext*>(opaque);
        if (memory_io_context->IsBlocking() == false) return AVERROR(EAGAIN);
        return 0;
    }

private:
    bool blocking_;
    size_t buffer_size_;
    std::unique_ptr<unsigned char, decltype(&av_free)> buffer_;
};

int main(int argc, char **argv)
{
    try
    {
        MemoryIoContext memory_io_context;
        std::unique_ptr<AVFormatContext, decltype(&avformat_free_context)> input_format_context(avformat_alloc_context(), avformat_free_context);
        if (input_format_context.get() == nullptr)  throw std::runtime_error("avformat_alloc_context() failed");
        auto io_context = memory_io_context.CreateAvIoContext();
        if (io_context== nullptr) throw std::runtime_error("avio_alloc_context() failed");
        input_format_context->pb = io_context.get();
        AVFormatContext *raw_input_format_context = input_format_context.get();
        try
        {
            // case 1: av_find_input_format() - avformat_open_input() will manually try to probe and locate actual tracks
            AVInputFormat *input_format = av_find_input_format("mp4");
            if (input_format == nullptr) throw std::runtime_error("av_find_input_format(\"mp4\") failed");
            auto error = avformat_open_input(&raw_input_format_context , "stream", input_format, NULL);
            if (error != 0)
            {
                // we know that the ffmpeg error code may be a negative fourcc value, so try to reverse it
                const auto error_bytes = -1 * error;
                fprintf(stderr, "avformat_open_input() returned %d (%.*s)", error, 4, reinterpret_cast<const char*>(&error_bytes));
                if (error == AVERROR_EOF)
                {
                    throw std::runtime_error("avformat_open_input() failed, EOF reached");
                }
                else if (error == AVERROR_INVALIDDATA)
                {
                    throw std::runtime_error("avformat_open_input() failed, invalid input data");
                }
                else if (error == AVERROR(EAGAIN))
                {
                    throw std::runtime_error("avformat_open_input() failed, input is not available at this state");
                }
                throw std::runtime_error("avformat_open_input() failed");
            }
        }
        catch (const std::exception &exception)
        {
            std::cerr << exception.what() << std::endl;
        }
        // TODO: case 2 - prefilled AVInputFormat
    }
    catch (const std::exception &exception)
    {
        std::cerr << exception.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}