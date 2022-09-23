#include "color.h"
#include "debug.h"
#include "macros.h"
#include "pixeldata.h"
#include <cstring>
#include <image.h>
#include <video.h>
#include <ascii.h>
#include <renderer.h>
#include <icons.h>
#include <media.h>
#include <thread>
#include <chrono>
#include <mutex>

#include <ncurses.h>
#include <curses_helpers.h>

const char KEY_ESCAPE = 27;

void render_thread(MediaPlayer* player, std::mutex* alterMutex) {
    std::unique_lock<std::mutex> alterLock(*alterMutex, std::defer_lock);
    WINDOW* inputWindow = newwin(0, 0, 1, 1);
    nodelay(inputWindow, true);
    keypad(inputWindow, true);
    std::chrono::milliseconds inputSleep = std::chrono::milliseconds(0);
    double jump_time_requested = 0;

    while (player->inUse) {
        alterLock.lock();
        inputSleep = std::chrono::milliseconds(1);
        int ch = wgetch(inputWindow);
        Playback* playback = player->timeline->playback;

        if (!player->inUse) {
            break;
        }

        if (jump_time_requested != 0 && (ch != KEY_LEFT && ch != KEY_RIGHT)) {
            double targetTime = playback->time + jump_time_requested;
            if (targetTime >= player->timeline->mediaData->duration) {
                player->inUse = false;
                alterLock.unlock();
                break;
            }

            jump_to_time(player->timeline, targetTime);
            video_symbol_stack_push(player->displayCache->symbol_stack, jump_time_requested < 0 ? get_video_symbol(BACKWARD_ICON) : get_video_symbol(FORWARD_ICON));
            jump_time_requested = 0;
        }

        if (ch == (int)(' ')) {
            playback->playing = !playback->playing;
        } else if (ch == (int)('d') || ch == (int)('D')) {
            player->displaySettings->show_debug = !player->displaySettings->show_debug;
        } else if (ch == (int)('c') || ch == (int)('C')) {
            player->displaySettings->mode = get_next_display_mode(player->displaySettings->mode);
        } else if (ch == KEY_LEFT) {
            jump_time_requested -= TIME_CHANGE_AMOUNT;
        } else if (ch == KEY_RIGHT) {
            jump_time_requested += TIME_CHANGE_AMOUNT;
        } else if (ch == KEY_UP) {
            playback->volume = std::min(1.0, playback->volume + VOLUME_CHANGE_AMOUNT);
            video_symbol_stack_push(player->displayCache->symbol_stack,get_symbol_from_volume(playback->volume));
        } else if (ch == KEY_DOWN) {
            playback->volume = std::min(1.0, playback->volume - VOLUME_CHANGE_AMOUNT);
            video_symbol_stack_push(player->displayCache->symbol_stack,get_symbol_from_volume(playback->volume));
        } else if (ch == (int)('n') || ch == (int)('N')) {
            playback->speed = std::min(5.0, playback->speed + PLAYBACK_SPEED_CHANGE_INTERVAL);
        } else if (ch == (int)('m') || ch == (int)('M')) {
            playback->speed = std::max(0.25, playback->speed - PLAYBACK_SPEED_CHANGE_INTERVAL);
        } else if (ch == (int)('x') || ch == (int)('X') || ch == KEY_ESCAPE) {
            player->inUse = !player->inUse;
        } else if (ch == KEY_RESIZE) {
            endwin();
            refresh();
        }

        render_screen(player);

        alterLock.unlock();
        refresh();
        std::this_thread::sleep_for(std::chrono::milliseconds(5) + inputSleep);
    }

    delwin(inputWindow);
}

void render_screen(MediaPlayer* player) {
    /* if (player->displaySettings->show_debug) { */
    /*     if (player->displaySettings->mode == DISPLAY_MODE_VIDEO) { */
    /*         render_video_debug(player); */
    /*     } else if (player->displaySettings->mode == DISPLAY_MODE_AUDIO) { */
    /*         render_audio_debug(player); */
    /*     } */
    /* } else { */
    /*     if (player->displaySettings->mode == DISPLAY_MODE_VIDEO) { */
    /*         if (player->displayCache->image != nullptr && player->displayCache->last_rendered_image != nullptr) { */
    /*             if (!pixel_data_equals(player->displayCache->image, player->displayCache->last_rendered_image)) { */
    /*                 render_movie_screen(player); */
    /*             } */
    /*         } */
    /*     } else if (player->displaySettings->mode == DISPLAY_MODE_AUDIO) { */
    /*         render_audio_screen(player); */
    /*     } */
    /* } */

    if (player->displayCache->image != nullptr && player->displayCache->last_rendered_image != nullptr) {
        if (!pixel_data_equals(player->displayCache->image, player->displayCache->last_rendered_image)) {
            render_movie_screen(player);
        }
    }

    if (player->displayCache->last_rendered_image != nullptr) {
        pixel_data_free(player->displayCache->last_rendered_image);
    }
    if (player->displayCache->image != nullptr) {
        player->displayCache->last_rendered_image = copy_pixel_data(player->displayCache->image);
    }

}

void render_audio_screen(MediaPlayer *player) {
    erase();
    printw("AUDIO");
}

void print_debug(MediaDebugInfo* debug_info, const char* source, const char* type) {
    for (int i = 0; i < debug_info->nb_messages; i++) {
        if (std::strcmp(source, debug_info->messages[i]->source) == 0 && std::strcmp(type, debug_info->messages[i]->type) == 0) {
            printw("%s", debug_info->messages[i]->message);
        }
    }
}

void render_audio_debug(MediaPlayer* player) {
    erase();
    print_debug(player->displayCache->debug_info, "audio", "debug");
    if (player->displayCache->debug_info->nb_messages == MEDIA_DEBUG_MESSAGE_BUFFER_SIZE) {
        clear_media_debug(player->displayCache->debug_info, "audio", "debug");
    }
}

void render_video_debug(MediaPlayer *player) {
    erase();
    print_debug(player->displayCache->debug_info, "video", "debug");
    if (player->displayCache->debug_info->nb_messages == MEDIA_DEBUG_MESSAGE_BUFFER_SIZE) {
        clear_media_debug(player->displayCache->debug_info, "video", "debug");
    }
}


void render_movie_screen(MediaPlayer* player) {
    erase();
    if (player->displayCache->image != nullptr) {
        VideoSymbolStack* symbol_stack = player->displayCache->symbol_stack;
        AsciiImage* textImage = get_ascii_image_bounded(player->displayCache->image, COLS, LINES);

        while (player->displayCache->symbol_stack->top >= 0) {
            VideoSymbol* currentSymbol = video_symbol_stack_peek(symbol_stack); 
            if (std::chrono::steady_clock::now() - currentSymbol->startTime < currentSymbol->lifeTime) {
                int currentSymbolFrame = get_video_symbol_current_frame(currentSymbol); 
                AsciiImage* symbolImage = get_ascii_image_bounded(currentSymbol->frameData[currentSymbolFrame], textImage->width, textImage->height);
                overlap_ascii_images(textImage, symbolImage);
                ascii_image_free(symbolImage);
                break;
            } else {
                video_symbol_stack_erase_pop(symbol_stack);
            }
        }

        if (!player->timeline->playback->playing) {
            VideoSymbol* pauseSymbol = get_video_symbol(PAUSE_ICON);
            AsciiImage* symbolImage = get_ascii_image_bounded(pauseSymbol->frameData[0], textImage->width, textImage->height);
            overlap_ascii_images(textImage, symbolImage);
            free_video_symbol(pauseSymbol);
            ascii_image_free(symbolImage);
        }

        /* const int palette_size = 16; */
        /* int actual_output_size; */
        /* rgb trained_colors[palette_size]; */
        /* get_most_common_colors(trained_colors, palette_size, textImage->color_data, textImage->width * textImage->height, &actual_output_size); */
        /* initialize_new_colors(trained_colors, actual_output_size); */

        /* printw("Actual Output Size: %d\n", actual_output_size); */
        /* for (int i = 0; i < palette_size; i++) { */
        /*     printw("Color #%d: %d %d %d\n", i, trained_colors[i][0], trained_colors[i][1], trained_colors[i][2]); */
        /* } */


        print_ascii_image_full(textImage);
        ascii_image_free(textImage);
    }
}

void fill_with_char(char* str, int len, char ch) {
    for (int i = 0; i < len; i++) {
        str[i] = ch;
    }
}

typedef struct ScreenChar {
    char character;
    int row;
    int col;
} ScreenChar;

void print_ascii_image_full(AsciiImage* textImage) {
    move(0, 0);
    const int screen_cols = COLS;
    const int screen_lines = LINES;
    int horizontalPaddingWidth = (screen_cols - textImage->width) / 2; 
    int verticalPaddingHeight = (screen_lines - textImage->height) / 2;
    char horizontalPadding[horizontalPaddingWidth + 1];
    horizontalPadding[horizontalPaddingWidth] = '\0';
    char lineAcross[screen_cols + 1];
    lineAcross[screen_cols] = '\0';

    fill_with_char(horizontalPadding, horizontalPaddingWidth, ' ');
    fill_with_char(lineAcross, screen_cols, ' ');

    for (int i = 0; i < verticalPaddingHeight; i++) {
        printw("%s\n", lineAcross);
    }

    const int max_color_pairs = std::min(COLORS - 8, COLOR_PAIRS);

    if (textImage->colored && max_color_pairs > 16) {
        ScreenChar* char_buckets[COLOR_PAIRS];
        int char_buckets_lengths[COLOR_PAIRS];
        int char_buckets_capacities[COLOR_PAIRS];
        for (int i = 0; i < COLOR_PAIRS; i++) {
            char_buckets[i] = nullptr;
            char_buckets_lengths[i] = 0;
            char_buckets_capacities[i] = 0;
        }

        for (int row = 0; row < textImage->height; row++) {
            for (int col = 0; col < textImage->width; col++) {
                int target_pair = get_closest_color_pair(textImage->color_data[row * textImage->width + col]);
                if (char_buckets[target_pair] == nullptr) {
                    char_buckets[target_pair] = (ScreenChar*)malloc(sizeof(ScreenChar) * 10);
                    if (char_buckets[target_pair] == NULL) {
                        char_buckets[target_pair] = nullptr;
                        continue;
                    }
                    char_buckets_capacities[target_pair] = 10;
                }

                if (char_buckets_lengths[target_pair] >= char_buckets_capacities[target_pair]) {
                    ScreenChar* tmp = (ScreenChar*)realloc(char_buckets[target_pair], sizeof(ScreenChar) * char_buckets_capacities[target_pair] * 2);
                    if (tmp != NULL) {
                        char_buckets[target_pair] = tmp;
                        char_buckets_capacities[target_pair] *= 2;
                    }
                }

                if (char_buckets_lengths[target_pair] < char_buckets_capacities[target_pair]) {
                    char_buckets[target_pair][char_buckets_lengths[target_pair]] = { textImage->lines[row * textImage->width + col], row, col };
                    char_buckets_lengths[target_pair] += 1;
                }
            }
        }

        for (int b = 0; b < COLOR_PAIRS; b++) {
            if (char_buckets[b] == nullptr || char_buckets[b] == NULL) {
                continue;
            }

            attron(COLOR_PAIR(b));
            for (int i = 0; i < char_buckets_lengths[b]; i++) {
                mvaddch(verticalPaddingHeight + char_buckets[b][i].row, horizontalPaddingWidth + char_buckets[b][i].col, char_buckets[b][i].character);
            }

            if (char_buckets[b] != nullptr && char_buckets[b] != NULL) {
                free(char_buckets[b]);
            }
            attroff(COLOR_PAIR(b));
        }
    } else {
        for (int row = 0; row < std::min(textImage->height, LINES); row++) {
            printw("%s|", horizontalPadding);
            for (int col = 0; col < std::min(textImage->width, COLS); col++) {
                addch(textImage->lines[row * textImage->width + col]);
            }
            addstr("|\n");
        }
    }
}
