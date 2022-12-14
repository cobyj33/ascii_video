#include <curses.h>
#include <libavutil/avutil.h>
#include <pthread.h>
#include <loader.h>
#include <media.h>
#include <macros.h>
#include <threads.h>

void* data_loading_thread(void* args) {
    MediaThreadData* thread_data = (MediaThreadData*)args;
    MediaPlayer* player = thread_data->player;
    pthread_mutex_t* alterMutex = thread_data->alterMutex;

    int shouldFetch = 1;
    MediaData* media_data = player->timeline->mediaData;

    while (!player->timeline->mediaData->allPacketsRead) {
        pthread_mutex_lock(alterMutex);
        shouldFetch = 1;

        for (int i = 0; i < media_data->nb_streams; i++) {
            if (selection_list_length(media_data->media_streams[i]->packets) > PACKET_RESERVE_SIZE) {
                shouldFetch = 0;
                break;
            }
        }

        if (shouldFetch) {
            fetch_next(media_data, 20);
        }

        pthread_mutex_unlock(alterMutex);
        sleep_for_ms(30);
    }

    return NULL;
}


void fetch_next(MediaData* media_data, int requestedPacketCount) {
    AVPacket* readingPacket = av_packet_alloc();
    int packetsRead = 0;

    while (av_read_frame(media_data->formatContext, readingPacket) == 0) {
        media_data->currentPacket++;
        if (packetsRead >= requestedPacketCount) {
            return;
        }

        for (int i = 0; i < media_data->nb_streams; i++) {
            if (media_data->media_streams[i]->info->stream->index == readingPacket->stream_index) {
                AVPacket* savedPacket = av_packet_alloc();
                av_packet_ref(savedPacket, readingPacket);
                selection_list_push_back(media_data->media_streams[i]->packets, savedPacket);
                /* erase(); */
                /* printw("Packet List For %s: %d", av_get_media_type_string(media_data->media_streams[i]->info->mediaType), selection_list_length(media_data->media_streams[i]->packets)); */
                /* refresh(); */
                break;
            }
        }

        av_packet_unref(readingPacket);
    }

    media_data->allPacketsRead = 1;
    av_packet_free(&readingPacket);
}
