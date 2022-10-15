/*
 * Copyright (C) 2021 Linux Studio Plugins Project <https://lsp-plug.in/>
 *           (C) 2021 Vladimir Sadovnikov <sadko4u@gmail.com>
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

            protected:
                struct afsample_t
                {
                    dspu::Sample       *pSource;                                        // Source sample (unchanged)
                    dspu::Sample       *pSample;                                        // Sample for playback (processed)
                    float              *vThumbs[meta::sampler_metadata::TRACKS_MAX];    // List of thumbnails
                };

                enum afindex_t
                {
                    AFI_CURR,
                    AFI_NEW,
                    AFI_OLD,
                    AFI_TOTAL
                };

                enum stretch_fade_t
                {
                    XFADE_LINEAR,
                    XFADE_CONST_POWER,
                    XFADE_DFL = XFADE_CONST_POWER
                };

                struct afile_t
                {
                    size_t              nID;                                            // ID of sample
                    AFLoader           *pLoader;                                        // Audio file loader task
                    dspu::Toggle        sListen;                                        // Listen toggle
                    dspu::Blink         sNoteOn;                                        // Note on led

                    bool                bDirty;                                         // Dirty flag
                    bool                bSync;                                          // Sync flag
                    float               fVelocity;                                      // Velocity
                    float               fPitch;                                         // Pitch (st)
                    float               fStretch;                                       // Stretch (sec)
                    float               fStretchStart;                                  // Stretch start (ms)
                    float               fStretchEnd;                                    // Stretch end (ms)
                    float               fStretchChunk;                                  // Stretch chunk (bar)
                    float               fStretchFade;                                   // Stretch fade
                    size_t              nStretchFadeType;                               // Stretch fade type
                    float               fHeadCut;                                       // Head cut (ms)
                    float               fTailCut;                                       // Tail cut (ms)
                    float               fFadeIn;                                        // Fade In (ms)
                    float               fFadeOut;                                       // Fade Out (ms)
                    bool                bReverse;                                       // Reverse sample
                    bool                bCompensate;                                    // Compensate time 
                    float               fPreDelay;                                      // Pre-delay
                    float               fMakeup;                                        // Makeup gain
                    float               fGains[meta::sampler_metadata::TRACKS_MAX];     // List of gain values
                    float               fStretchStartOut;                               // Actual position of the stretch start position
                    float               fStretchEndOut;                                 // Actual position of the stretch end position
                    float               fLength;                                        // Length of processed sample in milliseconds
                    status_t            nStatus;                                        // Loading status
                    bool                bOn;                                            // On flag

                    plug::IPort        *pFile;                                          // Audio file port
                    plug::IPort        *pPitch;                                         // Pitch
                    plug::IPort        *pStretch;                                       // Stretch
                    plug::IPort        *pStretchStart;                                  // Stretch start
                    plug::IPort        *pStretchEnd;                                    // Stretch end
                    plug::IPort        *pStretchChunk;                                  // Stretch chunk
                    plug::IPort        *pStretchFade;                                   // Stretch fade
                    plug::IPort        *pStretchFadeType;                               // Stretch fade type
                    plug::IPort        *pHeadCut;                                       // Head cut
                    plug::IPort        *pTailCut;                                       // Tail cut
                    plug::IPort        *pFadeIn;                                        // Fade in length
                    plug::IPort        *pFadeOut;                                       // Fade out length
                    plug::IPort        *pMakeup;                                        // Makup gain
                    plug::IPort        *pVelocity;                                      // Velocity range top
                    plug::IPort        *pPreDelay;                                      // Pre-delay
                    plug::IPort        *pListen;                                        // Listen trigger
                    plug::IPort        *pReverse;                                       // Reverse sample
                    plug::IPort        *pCompensate;                                    // Compensate
                    plug::IPort        *pGains[meta::sampler_metadata::TRACKS_MAX];     // List of gain ports
                    plug::IPort        *pStretchStartOut;                               // Actual position of the stretch start position
                    plug::IPort        *pStretchEndOut;                                 // Actual position of the stretch end position
                    plug::IPort        *pLength;                                        // Length of the file
                    plug::IPort        *pStatus;                                        // Status of the file
                    plug::IPort        *pMesh;                                          // Dump of the file data
                    plug::IPort        *pNoteOn;                                        // Note on flag
                    plug::IPort        *pOn;                                            // Sample on flag
                    plug::IPort        *pActive;                                        // Sample activity flag

                    afsample_t         *vData[AFI_TOTAL];                               // Currently used audio file
                };

            protected:
                ipc::IExecutor     *pExecutor;                                          // Executor service
                afile_t            *vFiles;                                             // List of audio files
                afile_t           **vActive;                                            // List of active audio files
                dspu::SamplePlayer  vChannels[meta::sampler_metadata::TRACKS_MAX];      // List of channels
                dspu::Bypass        vBypass[meta::sampler_metadata::TRACKS_MAX];        // List of bypasses
                dspu::Blink         sActivity;                                          // Note on led for instrument
                dspu::Toggle        sListen;                                            // Listen toggle
                dspu::Randomizer    sRandom;                                            // Randomizer

                size_t              nFiles;                                             // Number of files
                size_t              nActive;                                            // Number of active files
                size_t              nChannels;                                          // Number of audio channels (mono/stereo)
                float              *vBuffer;                                            // Buffer
                bool                bBypass;                                            // Bypass flag
                bool                bReorder;                                           // Reorder flag
                float               fFadeout;                                           // Fadeout in milliseconds
                float               fDynamics;                                          // Dynamics
                float               fDrift;                                             // Time drifting
                size_t              nSampleRate;                                        // Sample rate

                plug::IPort        *pDynamics;                                          // Dynamics port
                plug::IPort        *pDrift;                                             // Time drifting port
                plug::IPort        *pActivity;                                          // Activity port
                plug::IPort        *pListen;                                            // Listen trigger
                uint8_t            *pData;                                              // Pointer to aligned data

            protected:
                void        destroy_state();
                void        destroy_afsample(afsample_t *af);
                int         load_file(afile_t *file);
                void        copy_asample(afsample_t *dst, const afsample_t *src);
                void        clear_asample(afsample_t *dst);
                bool        do_render_sample(afile_t *af);
                void        render_sample(afile_t *af);
                void        reorder_samples();
                void        process_listen_events();
                void        output_parameters(size_t samples);
                void        process_file_load_requests();
                void        play_sample(const afile_t *af, float gain, size_t delay);
                void        cancel_sample(const afile_t *af, size_t fadeout, size_t delay);

                template <class T>
                static void commit_afile_value(afile_t *af, T & field, plug::IPort *port);
                static void commit_afile_value(afile_t *af, bool & field, plug::IPort *port);

            protected:
                void        dump_afile(dspu::IStateDumper *v, const afile_t *f) const;
                void        dump_afsample(dspu::IStateDumper *v, const afsample_t *f) const;

            public:
                explicit sampler_kernel();
                virtual ~sampler_kernel();

            public:
                void        trigger_on(size_t timestamp, float level);
                void        trigger_off(size_t timestamp, float level);
                void        trigger_stop(size_t timestamp);

            public:
                void        set_fadeout(float length);

            public:
                bool        init(ipc::IExecutor *executor, size_t files, size_t channels);
                size_t      bind(plug::IPort **ports, size_t port_id, bool dynamics);
                void        bind_activity(plug::IPort *activity);
                void        destroy();

                void        update_settings();
                void        update_sample_rate(long sr);
                void        sync_samples_with_ui();

                /** Process the sampler kernel
                 *
                 * @param outs list of outputs (should be not the sampe as ins)
                 * @param ins list of inputs, elements may be NULL
                 * @param samples number of samples to process
                 */
                void        process(float **outs, const float **ins, size_t samples);

                void        dump(dspu::IStateDumper *v) const;
        };
    } /* namespace plugins */
} /* namespace lsp */

#endif /* PRIVATE_PLUGINS_SAMPLER_KERNEL_H_ */
