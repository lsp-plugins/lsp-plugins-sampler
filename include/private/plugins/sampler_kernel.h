/*
 * Copyright (C) 2025 Linux Studio Plugins Project <https://lsp-plug.in/>
 *           (C) 2025 Vladimir Sadovnikov <sadko4u@gmail.com>
 *
 * This file is part of lsp-plugins-sampler
 * Created on: 12 июл. 2021 г.
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

#ifndef PRIVATE_PLUGINS_SAMPLER_KERNEL_H_
#define PRIVATE_PLUGINS_SAMPLER_KERNEL_H_

#include <lsp-plug.in/plug-fw/plug.h>
#include <lsp-plug.in/dsp-units/ctl/Toggle.h>
#include <lsp-plug.in/dsp-units/ctl/Bypass.h>
#include <lsp-plug.in/dsp-units/ctl/Blink.h>
#include <lsp-plug.in/dsp-units/sampling/Sample.h>
#include <lsp-plug.in/dsp-units/sampling/SamplePlayer.h>
#include <lsp-plug.in/dsp-units/util/Randomizer.h>
#include <lsp-plug.in/lltl/parray.h>
#include <lsp-plug.in/ipc/ITask.h>
#include <private/meta/sampler.h>

namespace lsp
{
    namespace plugins
    {
        /**
         * Sampler implementation for single-channel audio sampler
         */
        class sampler_kernel
        {
            protected:
                struct afile_t;

                class AFLoader: public ipc::ITask
                {
                    private:
                        sampler_kernel         *pCore;
                        afile_t                *pFile;

                    public:
                        explicit AFLoader(sampler_kernel *base, afile_t *descr);
                        virtual ~AFLoader();

                    public:
                        virtual status_t        run();
                        void                    dump(dspu::IStateDumper *v) const;
                };

                class AFRenderer: public ipc::ITask
                {
                    private:
                        sampler_kernel         *pCore;
                        afile_t                *pFile;

                    public:
                        explicit AFRenderer(sampler_kernel *base, afile_t *descr);
                        virtual ~AFRenderer();

                    public:
                        virtual status_t        run();
                        void                    dump(dspu::IStateDumper *v) const;
                };

                class GCTask: public ipc::ITask
                {
                    private:
                        sampler_kernel         *pCore;

                    public:
                        explicit GCTask(sampler_kernel *base);
                        virtual ~GCTask();

                    public:
                        virtual status_t        run();
                        void                    dump(dspu::IStateDumper *v) const;
                };

            protected:
                enum crossfade_t
                {
                    XFADE_LINEAR,
                    XFADE_CONST_POWER,
                    XFADE_DFL = XFADE_CONST_POWER
                };

                enum play_mode_t
                {
                    PLAY_NOTE,
                    PLAY_INSTRUMENT,
                    PLAY_FILE
                };

                enum loop_mode_t
                {
                    LOOP_DIRECT,
                    LOOP_REVERSE,
                    LOOP_DIRECT_HALF_PP,
                    LOOP_REVERSE_HALF_PP,
                    LOOP_DIRECT_FULL_PP,
                    LOOP_REVERSE_FULL_PP,
                    LOOP_DIRECT_SMART_PP,
                    LOOP_REVERSE_SMART_PP
                };

                struct render_params_t
                {
                    ssize_t             nLength;                                        // Length before head & tail cut
                    ssize_t             nHeadCut;                                       // The amount of head cut
                    ssize_t             nTailCut;                                       // The amount of tail cut
                    ssize_t             nCutLength;                                     // Length after head & tail cut
                    ssize_t             nStretchDelta;                                  // Stretch delta
                    ssize_t             nStretchStart;                                  // Stretch start position
                    ssize_t             nStretchEnd;                                    // Stretch end position
                };

                struct afile_t
                {
                    uint32_t            nID;                                            // ID of sample
                    AFLoader           *pLoader;                                        // Audio file loader task
                    AFRenderer         *pRenderer;                                      // Audio file renderer task
                    dspu::Toggle        sListen;                                        // Listen sample preview toggle
                    dspu::Toggle        sStop;                                          // Stop listen sample preview toggle
                    dspu::Blink         sNoteOn;                                        // Note on led
                    dspu::Playback      vPlayback[4];                                   // Active playback handle
                    dspu::Playback      vListen[4];                                     // Listen playback handle
                    dspu::Sample       *pOriginal;                                      // Source sample (original, as from source file)
                    dspu::Sample       *pProcessed;                                     // Processed sample
                    float              *vThumbs[meta::sampler_metadata::TRACKS_MAX];    // List of thumbnails

                    uint32_t            nUpdateReq;                                     // Update request
                    uint32_t            nUpdateResp;                                    // Update response
                    bool                bSync;                                          // Sync flag
                    float               fMinVelocity;                                   // Minimum velocity
                    float               fMaxVelocity;                                   // Maximum velocity
                    float               fPitch;                                         // Pitch (st)
                    bool                bStretchOn;                                     // Stretch enabled
                    float               fStretch;                                       // Stretch (sec)
                    float               fStretchStart;                                  // Stretch start (ms)
                    float               fStretchEnd;                                    // Stretch end (ms)
                    float               fStretchChunk;                                  // Stretch chunk (bar)
                    float               fStretchFade;                                   // Stretch cross-fade length
                    uint32_t            nStretchFadeType;                               // Stretch cross-fade type
                    dspu::sample_loop_t enLoopMode;                                     // Loop mode
                    float               fLoopStart;                                     // Stretch start (ms)
                    float               fLoopEnd;                                       // Stretch end (ms)
                    float               fLoopFade;                                      // Loop cross-fade length
                    uint32_t            nLoopFadeType;                                  // Loop cross-fade type
                    float               fHeadCut;                                       // Head cut (ms)
                    float               fTailCut;                                       // Tail cut (ms)
                    float               fFadeIn;                                        // Fade In (ms)
                    float               fFadeOut;                                       // Fade Out (ms)
                    bool                bPreReverse;                                    // Pre-reverse sample
                    bool                bPostReverse;                                   // Post-reverse sample
                    bool                bCompensate;                                    // Compensate time
                    float               fCompensateFade;                                // Compensate fade
                    float               fCompensateChunk;                               // Compensate chunk
                    uint32_t            nCompensateFadeType;                            // Compensate fade type
                    float               fPreDelay;                                      // Pre-delay
                    float               fMakeup;                                        // Makeup gain
                    float               fGains[meta::sampler_metadata::TRACKS_MAX];     // List of gain values
                    float               fLength;                                        // Length of source sample in milliseconds
                    float               fActualLength;                                  // Length of processed sample in milliseconds
                    status_t            nStatus;                                        // Loading status
                    bool                bOn;                                            // On flag

                    plug::IPort        *pFile;                                          // Audio file port
                    plug::IPort        *pPitch;                                         // Pitch

                    plug::IPort        *pStretchOn;                                     // Stretch enabled
                    plug::IPort        *pStretch;                                       // Stretch amount
                    plug::IPort        *pStretchStart;                                  // Start of the stretch region
                    plug::IPort        *pStretchEnd;                                    // End of the stretch region
                    plug::IPort        *pStretchChunk;                                  // Stretch chunk
                    plug::IPort        *pStretchFade;                                   // Stretch cross-fade length
                    plug::IPort        *pStretchFadeType;                               // Stretch cross-fade type

                    plug::IPort        *pLoopOn;                                        // Loop enabled
                    plug::IPort        *pLoopMode;                                      // Loop mode
                    plug::IPort        *pLoopStart;                                     // Start of the loop region
                    plug::IPort        *pLoopEnd;                                       // End of the loop region
                    plug::IPort        *pLoopFadeType;                                  // Loop cross-fade type
                    plug::IPort        *pLoopFade;                                      // Loop cross-fade length

                    plug::IPort        *pHeadCut;                                       // Head cut
                    plug::IPort        *pTailCut;                                       // Tail cut
                    plug::IPort        *pFadeIn;                                        // Fade in length
                    plug::IPort        *pFadeOut;                                       // Fade out length
                    plug::IPort        *pMakeup;                                        // Makup gain

                    plug::IPort        *pEnvelopeOn;                                    // Enable envelope
                    plug::IPort        *pEnvelopeHoldOn;                                // Enable Hold point
                    plug::IPort        *pEnvelopeBreakOn;                               // Enable Break point
                    plug::IPort        *pEnvelopeAttackTime;                            // Attack time
                    plug::IPort        *pEnvelopeHoldTime;                              // Hold time
                    plug::IPort        *pEnvelopeDecayTime;                             // Decay time
                    plug::IPort        *pEnvelopeSlopeTime;                             // Slope time
                    plug::IPort        *pEnvelopeReleaseTime;                           // Release time
                    plug::IPort        *pEnvelopeBreakLevel;                            // Break level
                    plug::IPort        *pEnvelopeSustainLevel;                          // Sustain level
                    plug::IPort        *pEnvelopeAttackCurve;                           // Attack curvature
                    plug::IPort        *pEnvelopeDecayCurve;                            // Decay curvature
                    plug::IPort        *pEnvelopeSlopeCurve;                            // Slope curvature
                    plug::IPort        *pEnvelopeReleaseCurve;                          // Release curvature
                    plug::IPort        *pEnvelopeAttackType;                            // Attack curve type
                    plug::IPort        *pEnvelopeDecayType;                             // Decay curve type
                    plug::IPort        *pEnvelopeSlopeType;                             // Slope curve type
                    plug::IPort        *pEnvelopeReleaseType;                           // Release curve type

                    plug::IPort        *pVelocity;                                      // Velocity range top
                    plug::IPort        *pPreDelay;                                      // Pre-delay
                    plug::IPort        *pOn;                                            // Sample on outputflag
                    plug::IPort        *pListen;                                        // Listen sample preview
                    plug::IPort        *pStop;                                          // Stop listen sample preview
                    plug::IPort        *pPreReverse;                                    // Pre-reverse sample
                    plug::IPort        *pPostReverse;                                   // Post-reverse sample
                    plug::IPort        *pCompensate;                                    // Compensate
                    plug::IPort        *pCompensateFade;                                // Compensate fade
                    plug::IPort        *pCompensateChunk;                               // Compensate chunk
                    plug::IPort        *pCompensateFadeType;                            // Compensate fade type
                    plug::IPort        *pGains[meta::sampler_metadata::TRACKS_MAX];     // List of gain ports
                    plug::IPort        *pActive;                                        // Sample activity flag
                    plug::IPort        *pPlayPosition;                                  // Output current playback position
                    plug::IPort        *pNoteOn;                                        // Note on flag
                    plug::IPort        *pLength;                                        // Length of the file
                    plug::IPort        *pActualLength;                                  // Actual length of the file
                    plug::IPort        *pStatus;                                        // Status of the file
                    plug::IPort        *pMesh;                                          // Dump of the file data
                };

            protected:
                ipc::IExecutor     *pExecutor;                                          // Executor service
                dspu::Sample       *pGCList;                                            // Garbage collection list
                afile_t            *vFiles;                                             // List of audio files
                afile_t           **vActive;                                            // List of active audio files
                dspu::SamplePlayer  vChannels[meta::sampler_metadata::TRACKS_MAX];      // List of channels
                dspu::Bypass        vBypass[meta::sampler_metadata::TRACKS_MAX];        // List of bypasses
                dspu::Playback      vListen[4];                                         // Listen playback handle for instrument
                dspu::Blink         sActivity;                                          // Note on led for instrument
                dspu::Toggle        sListen;                                            // Listen sample preview toggle
                dspu::Toggle        sStop;                                              // Stop listen sample preview toggle
                dspu::Randomizer    sRandom;                                            // Randomizer
                GCTask              sGCTask;                                            // Garbage collection task

                size_t              nFiles;                                             // Number of files
                size_t              nActive;                                            // Number of active files
                size_t              nChannels;                                          // Number of audio channels (mono/stereo)
                float              *vBuffer;                                            // Buffer
                bool                bBypass;                                            // Bypass flag
                bool                bReorder;                                           // Reorder flag
                bool                bHandleVelocity;                                    // Velocity handling flag
                float               fFadeout;                                           // Fadeout in milliseconds
                float               fDynamics;                                          // Dynamics
                float               fDrift;                                             // Time drifting
                size_t              nSampleRate;                                        // Sample rate

                plug::IPort        *pDynamics;                                          // Dynamics port
                plug::IPort        *pHandleVelocity;                                    // Velocity handling
                plug::IPort        *pDrift;                                             // Time drifting port
                plug::IPort        *pActivity;                                          // Activity port
                plug::IPort        *pListen;                                            // Listen sample preview
                plug::IPort        *pStop;                                              // Stop listen sample preview
                uint8_t            *pData;                                              // Pointer to aligned data

            protected:
                void        destroy_state();
                status_t    load_file(afile_t *file);
                status_t    render_sample(afile_t *af);
                void        play_sample(afile_t *af, float gain, size_t delay, play_mode_t mode, bool listen);
                void        cancel_sample(afile_t *af, size_t delay);
                void        start_listen_file(afile_t *af, float gain);
                void        stop_listen_file(afile_t *af, bool force);
                void        start_listen_instrument(float velocity, float gain);
                void        stop_listen_instrument(bool force);

                void        process_file_load_requests();
                void        process_file_render_requests();
                void        process_gc_tasks();
                void        reorder_samples();
                void        process_listen_events();
                void        play_samples(float **listen, float **outs, const float **ins, size_t samples);
                void        output_parameters(size_t samples);
                afile_t    *select_active_sample(float velocity);

                template <class T>
                static void commit_value(uint32_t & counter, T & field, plug::IPort *port);
                static void commit_value(uint32_t & counter, bool & field, plug::IPort *port);

            protected:
                static void                 unload_afile(afile_t *file);
                static void                 destroy_afile(afile_t *af);
                static void                 destroy_samples(dspu::Sample *gc_list);
                static void                 destroy_sample(dspu::Sample * &sample);
                static ssize_t              compute_loop_point(const dspu::Sample *s, size_t position);
                static dspu::sample_loop_t  decode_loop_mode(plug::IPort *on, plug::IPort *mode);
                float                       compute_play_position(const afile_t *f);
                void                        dump_afile(dspu::IStateDumper *v, const afile_t *f) const;
                void                        perform_gc();

            public:
                explicit sampler_kernel();
                sampler_kernel(const sampler_kernel &) = delete;
                sampler_kernel(sampler_kernel &&) = delete;
                virtual ~sampler_kernel();

                sampler_kernel & operator = (const sampler_kernel &) = delete;
                sampler_kernel & operator = (sampler_kernel &&) = delete;

            public:
                void        trigger_on(size_t timestamp, uint8_t midi_velocity);
                void        trigger_off(size_t timestamp, bool handle);
                void        trigger_cancel(size_t timestamp);

            public:
                void        set_fadeout(float length);

            public:
                bool        init(ipc::IExecutor *executor, size_t files, size_t channels);
                void        bind(plug::IPort **ports, size_t & port_id, bool dynamics);
                void        bind_activity(plug::IPort **ports, size_t & port_id);
                void        destroy();

                void        update_settings();
                void        update_sample_rate(long sr);
                void        sync_samples_with_ui();

                /** Process the sampler kernel
                 *
                 * @param lisents list of outputs for listen events
                 * @param outs list of outputs (should be not the sampe as ins)
                 * @param ins list of inputs, elements may be NULL
                 * @param samples number of samples to process
                 */
                void        process(float **listens, float **outs, const float **ins, size_t samples);

                void        dump(dspu::IStateDumper *v) const;
        };
    } /* namespace plugins */
} /* namespace lsp */

#endif /* PRIVATE_PLUGINS_SAMPLER_KERNEL_H_ */
