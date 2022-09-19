/*
 * Copyright (C) 2021 Linux Studio Plugins Project <https://lsp-plug.in/>
 *           (C) 2021 Vladimir Sadovnikov <sadko4u@gmail.com>
 *
 * This file is part of lsp-plugins-sampler
 * Created on: 11 июл. 2021 г.
 *
 * lsp-plugins-sampler is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * lsp-plugins-sampler is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with lsp-plugins-sampler. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef PRIVATE_META_SAMPLER_H_
#define PRIVATE_META_SAMPLER_H_

#include <lsp-plug.in/plug-fw/meta/types.h>
#include <lsp-plug.in/plug-fw/const.h>

namespace lsp
{
    namespace meta
    {
        //-------------------------------------------------------------------------
        // Sampler metadata
        struct sampler_metadata
        {
            static constexpr float SAMPLE_PITCH_MIN         = -24.0f;   // Minimum pitch (st)
            static constexpr float SAMPLE_PITCH_MAX         = 24.0f;    // Maximum pitch (st)
            static constexpr float SAMPLE_PITCH_DFL         = 0.0f;     // Pitch (st)
            static constexpr float SAMPLE_PITCH_STEP        = 0.01f;    // Pitch step (st)
                                                                        
            static constexpr const float SAMPLE_STRETCH_MIN           = -30.0f;   // Maximum stretch (sec)
            static constexpr const float SAMPLE_STRETCH_MAX           = 30.0f;    // Maximum stretch (sec)
            static constexpr const float SAMPLE_STRETCH_DFL           = 0.0f;     // Stretch (sec)
            static constexpr const float SAMPLE_STRETCH_STEP          = 0.01f;    // Stretch step (sec)
            
            static constexpr const float SAMPLE_STRETCH_CHUNK_MIN     = 4.0f;     // Minimum stretch chunk (bar)
            static constexpr const float SAMPLE_STRETCH_CHUNK_MAX     = 32.0f;    // Maximum stretch chunk (bar)
            static constexpr const float SAMPLE_STRETCH_CHUNK_DFL     = 9.0f;     // Stretch chunk (bar)
            static constexpr const float SAMPLE_STRETCH_CHUNK_STEP    = 0.1f;     // Stretch chunk step (bar)

            static constexpr const float SAMPLE_STRETCH_FADE_MIN     = 0.0f;      // Minimum stretch chunk (bar)
            static constexpr const float SAMPLE_STRETCH_FADE_MAX     = 100.0f;    // Maximum stretch chunk (bar)
            static constexpr const float SAMPLE_STRETCH_FADE_DFL     = 100.0f;    // Stretch chunk (bar)
            static constexpr const float SAMPLE_STRETCH_FADE_STEP    = 0.1f;      // Stretch chunk step (bar)
                                                                       
            static constexpr float SAMPLE_LENGTH_MIN        = 0.0f;     // Minimum length (ms)
            static constexpr float SAMPLE_LENGTH_MAX        = 64000.0f; // Maximum sample length (ms)
            static constexpr float SAMPLE_LENGTH_DFL        = 0.0f;     // Sample length (ms)
            static constexpr float SAMPLE_LENGTH_STEP       = 0.1f;     // Sample step (ms)

            static constexpr float PREDELAY_MIN             = 0.0f;     // Pre-delay min (ms)
            static constexpr float PREDELAY_MAX             = 100.0f;   // Pre-delay max (ms)
            static constexpr float PREDELAY_DFL             = 0.0f;     // Pre-delay default (ms)
            static constexpr float PREDELAY_STEP            = 0.1f;     // Pre-delay step (ms)

            static constexpr float FADEOUT_MIN              = 0.0f;     // Fade-out min (ms)
            static constexpr float FADEOUT_MAX              = 50.0f;    // Fade-out max (ms)
            static constexpr float FADEOUT_DFL              = 10.0f;    // Fade-out default (ms)
            static constexpr float FADEOUT_STEP             = 0.025f;   // Fade-out step (ms)

            static constexpr size_t MESH_SIZE               = 320;      // Maximum mesh size
            static constexpr size_t TRACKS_MAX              = 2;        // Maximum tracks per mesh/sample
            static constexpr float ACTIVITY_LIGHTING        = 0.1f;     // Activity lighting (seconds)

            static constexpr size_t CHANNEL_DFL             = 0;        // Default channel
            static constexpr size_t NOTE_DFL                = 9;        // A
            static constexpr size_t OCTAVE_DFL              = 4;        // 4th octave

            static constexpr float DRIFT_MIN                = 0.0f;     // Minimum delay
            static constexpr float DRIFT_DFL                = 0.0f;     // Default delay
            static constexpr float DRIFT_STEP               = 0.1f;     // Delay step
            static constexpr float DRIFT_MAX                = 100.0f;   // Maximum delay

            static constexpr float DYNA_MIN                 = 0.0f;     // Minimum dynamics
            static constexpr float DYNA_DFL                 = 0.0f;     // Default dynamics
            static constexpr float DYNA_STEP                = 0.1f;     // Dynamics step
            static constexpr float DYNA_MAX                 = 100.0f;   // Maximum dynamics

            static constexpr size_t PLAYBACKS_MAX           = 8192;     // Maximum number of simultaneously playing samples
            static constexpr size_t SAMPLE_FILES            = 8;        // Number of sample files
            static constexpr size_t BUFFER_SIZE             = 4096;     // Size of temporary buffer

            static constexpr size_t INSTRUMENTS_MAX         = 64;       // Maximum supported instruments

        };

        // Different samplers
        extern const plugin_t sampler_mono;
        extern const plugin_t sampler_stereo;
        extern const plugin_t multisampler_x12;
        extern const plugin_t multisampler_x24;
        extern const plugin_t multisampler_x48;
        extern const plugin_t multisampler_x12_do;
        extern const plugin_t multisampler_x24_do;
        extern const plugin_t multisampler_x48_do;
    }
}



#endif /* PRIVATE_META_SAMPLER_H_ */
