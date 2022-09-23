#include <ascii.h>
#include "pixeldata.h"
#include <image.h>
#include <libavutil/pixfmt.h>
#include <media.h>
#include <renderer.h>
#include <decode.h>
#include <boiler.h>

#include <ncurses.h>
#include <iostream>

#include <cstdint>
#include <cstdlib>
#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

int imageProgram(const char* fileName, bool use_colors) {
    erase();
    PixelData* pixelData = get_pixel_data_from_image(fileName, use_colors ? RGB24 : GRAYSCALE8);
    if (pixelData == nullptr) {
        std::cout << "Could not get image data from " << fileName;
        endwin();
        return EXIT_FAILURE;
    }

    AsciiImage* textImage = get_ascii_image_bounded(pixelData, COLS, LINES);
    if (textImage == nullptr) {
        pixel_data_free(pixelData);
        return EXIT_FAILURE;
    }

    print_ascii_image_full(textImage);
    pixel_data_free(pixelData); 
    ascii_image_free(textImage);
    refresh();
    getch();

    endwin();
    return EXIT_SUCCESS;
}

PixelData* get_pixel_data_from_image(const char* fileName, PixelDataFormat format) {
    MediaData* mediaData = media_data_alloc(fileName);
    if (mediaData == nullptr) {
        std::cout << "Could not fetch media data of " << fileName << std::endl;
        return nullptr;
    }

    MediaStream* imageStream = get_media_stream(mediaData, AVMEDIA_TYPE_VIDEO);
    if (imageStream == nullptr) {
        std::cerr << "Could not fetch image stream of " << fileName << std::endl;
        return nullptr;
    }

    AVCodecContext* codecContext = imageStream->info->codecContext;
    VideoConverter* imageConverter = get_video_converter(
            codecContext->width, codecContext->height, PixelDataFormat_to_AVPixelFormat(format),
            codecContext->width, codecContext->height, codecContext->pix_fmt
            );
    
    if (imageConverter == nullptr) {
        std::cout << "Could not initialize image color converter" << std::endl;
        media_data_free(mediaData);
        return nullptr;
    }

    AVPacket* packet = av_packet_alloc();
    AVFrame* finalFrame = nullptr;

    auto cleanup = [mediaData, packet, finalFrame, imageConverter](){
        av_packet_free((AVPacket**)&packet);
        av_frame_free((AVFrame**)&finalFrame);
        free_video_converter(imageConverter);
        media_data_free(mediaData);
    };

    int result;

    while (av_read_frame(mediaData->formatContext, packet) == 0) {
        int nb_out;
        if (packet->stream_index != imageStream->info->stream->index)
            continue;

        AVFrame** originalFrame = decode_video_packet(codecContext, packet, &result, &nb_out);
        if (result == AVERROR(EAGAIN)) {
            continue;
        } else if (result < 0) {
            std::cout << "ERROR WHILE READING PACKET DATA FROM IMAGE " << fileName << std::endl;
            cleanup();
            free_frame_list(originalFrame, nb_out);
            return nullptr;
        } else {
            finalFrame = convert_video_frame(imageConverter, originalFrame[0]);
            free_frame_list(originalFrame, nb_out);
            break;
        }
    }

    if (finalFrame == nullptr) {
        cleanup();
        return nullptr;
    }

    PixelData* data = pixel_data_alloc_from_frame(finalFrame);
    cleanup();
    return data;
}

PixelData* pixel_data_alloc_from_frame(AVFrame* videoFrame) {
    PixelData* data = pixel_data_alloc(videoFrame->width, videoFrame->height, AVPixelFormat_to_PixelDataFormat((AVPixelFormat)videoFrame->format));

    for (int i = 0; i < get_pixel_data_buffer_size(data); i++) {
        data->pixels[i] = videoFrame->data[0][i];
    }

    return data;
}

const char* pixel_data_format_string(PixelDataFormat format) {
    switch (format) {
        case RGB24: return "rgb24";
        case GRAYSCALE8: return "grayscale8";
        default: return "unknown";
    }
}

PixelData* copy_pixel_data(PixelData* data) {
    PixelData* new_data = pixel_data_alloc(data->width, data->height, data->format);
    for (int i = 0; i < get_pixel_data_buffer_size(data); i++) {
        new_data->pixels[i] = data->pixels[i];
    }
    return new_data;
}

int get_pixel_data_buffer_size(PixelData* data) {
    switch (data->format) {
        case RGB24: return data->width * data->height * 3 * sizeof(uint8_t);
        case GRAYSCALE8: return data->width * data->height * sizeof(uint8_t);
        default: return data->width * data->height;
    }
}


AVPixelFormat PixelDataFormat_to_AVPixelFormat(PixelDataFormat format) {
    switch (format) {
        case RGB24: return AV_PIX_FMT_RGB24;
        case GRAYSCALE8: return AV_PIX_FMT_GRAY8;
        default: return AV_PIX_FMT_GRAY8;
    }
}

PixelDataFormat AVPixelFormat_to_PixelDataFormat(AVPixelFormat format) {
    switch (format) {
        case AV_PIX_FMT_RGB24: return RGB24;
        case AV_PIX_FMT_GRAY8: return GRAYSCALE8;
        default: 
            char data_buf[128];
            av_get_pix_fmt_string(data_buf, 128, format);
            fprintf(stderr, "Could not get PixelDataFormat of %s\n", data_buf);
            return GRAYSCALE8;
    }
}

PixelData* pixel_data_alloc(int width, int height, PixelDataFormat format) {
  PixelData* pixelData = (PixelData*)malloc(sizeof(PixelData));
  pixelData->width = width;
  pixelData->height = height;
  pixelData->format = format;
  int buffer_size = get_pixel_data_buffer_size(pixelData);
  pixelData->pixels = (uint8_t*)malloc(buffer_size);

  for (int i = 0; i < buffer_size; i++) {
      pixelData->pixels[i] = (uint8_t)0;
  }

  return pixelData;
}

void pixel_data_free(PixelData* pixelData) {
  free(pixelData->pixels);
  free(pixelData);
}

bool pixel_data_equals(PixelData* first, PixelData* second) {
    if (!(first->width == second->width && first->height == second->height && first->format == second->format)) {
        return false;
    }

    for (int row = 0; row < first->height; row++) {
        for (int col = 0; col < first->width; col++) {
            if (first->format == RGB24) {
                for (int i = 0; i < 3; i++) {
                    if (first->pixels[row * first->width * 3 + col * 3 + i] != second->pixels[row * second->width * 3 + col * 3 + i]) {
                        return false;
                    }
                }
            } else if (first->format == GRAYSCALE8) {
                if (first->pixels[row * first->width + col] != second->pixels[row * second->width + col]) {
                    return false;
                }
            }
        }
    }

    return true;
}

void pixel_data_to_rgb(PixelData* data, rgb* output) {
    for (int row = 0; row < data->height; row++) {
        for (int col = 0; col < data->width; col++) {

            if (data->format == RGB24) {
                int pixel_index = row * data->width * 3 + col * 3;
                rgb pixel = { data->pixels[pixel_index], data->pixels[pixel_index + 1], data->pixels[pixel_index + 2] };
                rgb_copy(output[row * data->width + col], pixel); 
            } else if (data->format == GRAYSCALE8) {
                uint8_t value = data->pixels[row * data->width + col];
                rgb_set(output[row * data->width + col], value, value, value);
            }

        }
    }

}
