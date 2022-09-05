#pragma once

float** alterAudioSampleLength(float** originalSamples, int nb_samples, int nb_channels, int target_nb_samples);
float** stretchAudioSamples(float** originalSamples, int nb_samples, int nb_channels, int target_nb_samples);
float** shrinkAudioSamples(float** originalSamples, int nb_samples, int nb_channels, int target_nb_samples);
