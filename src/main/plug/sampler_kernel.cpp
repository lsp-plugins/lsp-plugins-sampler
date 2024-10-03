/*
 * Copyright (C) 2024 Linux Studio Plugins Project <https://lsp-plug.in/>
 *           (C) 2024 Vladimir Sadovnikov <sadko4u@gmail.com>
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

#include <lsp-plug.in/common/alloc.h>
#include <lsp-plug.in/common/atomic.h>
#include <lsp-plug.in/common/debug.h>
#include <lsp-plug.in/dsp-units/units.h>
#include <lsp-plug.in/dsp-units/misc/fade.h>
#include <lsp-plug.in/dsp-units/sampling/PlaySettings.h>
#include <lsp-plug.in/dsp/dsp.h>
#include <lsp-plug.in/shared/debug.h>

#include <private/plugins/sampler_kernel.h>

namespace lsp
{
    namespace plugins
    {
        //-------------------------------------------------------------------------
        sampler_kernel::AFLoader::AFLoader(sampler_kernel *base, afile_t *descr)
        {
            pCore       = base;
            pFile       = descr;
        }

        sampler_kernel::AFLoader::~AFLoader()
        {
            pCore       = NULL;
            pFile       = NULL;
        }

        status_t sampler_kernel::AFLoader::run()
        {
            return pCore->load_file(pFile);
        };

        void sampler_kernel::AFLoader::dump(dspu::IStateDumper *v) const
        {
            v->write("pCore", pCore);
            v->write("pFile", pFile);
        }

        //-------------------------------------------------------------------------
        sampler_kernel::AFRenderer::AFRenderer(sampler_kernel *base, afile_t *descr)
        {
            pCore       = base;
            pFile       = descr;
        }

        sampler_kernel::AFRenderer::~AFRenderer()
        {
            pCore       = NULL;
            pFile       = NULL;
        }

        status_t sampler_kernel::AFRenderer::run()
        {
            return pCore->render_sample(pFile);
        };

        void sampler_kernel::AFRenderer::dump(dspu::IStateDumper *v) const
        {
            v->write("pCore", pCore);
            v->write("pFile", pFile);
        }

        //-------------------------------------------------------------------------
        sampler_kernel::GCTask::GCTask(sampler_kernel *base)
        {
            pCore       = base;
        }

        sampler_kernel::GCTask::~GCTask()
        {
            pCore       = NULL;
        }


        status_t sampler_kernel::GCTask::run()
        {
            pCore->perform_gc();
            return STATUS_OK;
        }

        void sampler_kernel::GCTask::dump(dspu::IStateDumper *v) const
        {
            v->write("pCore", pCore);
        }

        //-------------------------------------------------------------------------
        sampler_kernel::sampler_kernel():
            sGCTask(this)
        {
            pExecutor       = NULL;
            pGCList         = NULL;
            vFiles          = NULL;
            vActive         = NULL;
            nFiles          = 0;
            nActive         = 0;
            nChannels       = 0;
            vBuffer         = NULL;
            bBypass         = false;
            bReorder        = false;
            fFadeout        = 10.0f;
            fDynamics       = meta::sampler_metadata::DYNA_DFL;
            fDrift          = meta::sampler_metadata::DRIFT_DFL;
            nSampleRate     = 0;

            pDynamics       = NULL;
            pDrift          = NULL;
            pActivity       = NULL;
            pListen         = NULL;
            pData           = NULL;
        }

        sampler_kernel::~sampler_kernel()
        {
            lsp_trace("this = %p", this);
            destroy_state();
        }

        void sampler_kernel::set_fadeout(float length)
        {
            fFadeout        = length;
        }

        bool sampler_kernel::init(ipc::IExecutor *executor, size_t files, size_t channels)
        {
            // Validate parameters
            channels        = lsp_min(channels, meta::sampler_metadata::TRACKS_MAX);

            // Now we may store values
            nFiles          = files;
            nChannels       = channels;
            bReorder        = true;
            nActive         = 0;
            pExecutor       = executor;

            // Now determine object sizes
            size_t afile_szof           = align_size(sizeof(afile_t) * files, DEFAULT_ALIGN);
            size_t vactive_szof         = align_size(sizeof(afile_t *) * files, DEFAULT_ALIGN);
            size_t vbuffer_szof         = align_size(sizeof(float) * meta::sampler_metadata::BUFFER_SIZE, DEFAULT_ALIGN);

            // Allocate raw chunk and link data
            size_t allocate             = afile_szof + vactive_szof + vbuffer_szof;
            uint8_t *ptr                = alloc_aligned<uint8_t>(pData, allocate);
            if (ptr == NULL)
                return false;
            lsp_guard_assert(
                uint8_t *tail               = &ptr[allocate];
            );

            // Allocate files
            vFiles                      = advance_ptr_bytes<afile_t>(ptr, afile_szof);
            vActive                     = advance_ptr_bytes<afile_t *>(ptr, vactive_szof);
            vBuffer                     = advance_ptr_bytes<float>(ptr, vbuffer_szof);

            for (size_t i=0; i<files; ++i)
            {
                afile_t *af                 = &vFiles[i];

                af->nID                     = i;
                af->pLoader                 = NULL;
                af->pRenderer               = NULL;

                af->sListen.construct();
                af->sNoteOn.construct();
                for (size_t i=0; i<4; ++i)
                {
                    af->vPlayback[i].construct();
                    af->vListen[i].construct();
                }
                af->pOriginal               = NULL;
                af->pProcessed              = NULL;
                for (size_t j=0; j<meta::sampler_metadata::TRACKS_MAX; ++j)
                    af->vThumbs[j]              = NULL;

                af->nUpdateReq              = 0;
                af->nUpdateResp             = 0;
                af->bSync                   = false;
                af->fVelocity               = 1.0f;
                af->fPitch                  = 0.0f;
                af->bStretchOn              = false;
                af->fStretch                = 0.0f;
                af->fStretchStart           = 0.0f;
                af->fStretchEnd             = 0.0f;
                af->fStretchChunk           = 0.0f;
                af->fStretchFade            = 0.0f;
                af->nStretchFadeType        = XFADE_DFL;
                af->enLoopMode              = dspu::SAMPLE_LOOP_NONE;
                af->fLoopStart              = 0.0f;
                af->fLoopEnd                = 0.0f;
                af->fLoopFade               = 0.0f;
                af->nLoopFadeType           = 0;
                af->fHeadCut                = 0.0f;
                af->fTailCut                = 0.0f;
                af->fFadeIn                 = 0.0f;
                af->fFadeOut                = 0.0f;
                af->bPreReverse             = false;
                af->bPostReverse            = false;
                af->bCompensate             = false;
                af->fCompensateFade         = 0.0f;
                af->fCompensateChunk        = 0.0f;
                af->nCompensateFadeType     = XFADE_DFL;
                af->fPreDelay               = meta::sampler_metadata::PREDELAY_DFL;
                af->fMakeup                 = 1.0f;
                for (size_t j=0; j<meta::sampler_metadata::TRACKS_MAX; ++j)
                    af->fGains[j]               = 0;
                af->fLength                 = 0.0f;
                af->fActualLength           = 0.0f;
                af->nStatus                 = STATUS_UNSPECIFIED;
                af->bOn                     = true;

                af->pFile                   = NULL;
                af->pPitch                  = NULL;
                af->pStretchOn              = NULL;
                af->pStretch                = NULL;
                af->pStretchStart           = NULL;
                af->pStretchEnd             = NULL;
                af->pStretchChunk           = NULL;
                af->pStretchFade            = NULL;
                af->pStretchFadeType        = NULL;
                af->pLoopOn                 = NULL;
                af->pLoopMode               = NULL;
                af->pLoopStart              = NULL;
                af->pLoopEnd                = NULL;
                af->pLoopFade               = NULL;
                af->pLoopFadeType           = NULL;
                af->pHeadCut                = NULL;
                af->pTailCut                = NULL;
                af->pFadeIn                 = NULL;
                af->pFadeOut                = NULL;
                af->pVelocity               = NULL;
                af->pMakeup                 = NULL;
                af->pPreDelay               = NULL;
                af->pOn                     = NULL;
                af->pListen                 = NULL;
                af->pPreReverse             = NULL;
                af->pPostReverse            = NULL;
                af->pCompensate             = NULL;
                af->pCompensateFade         = NULL;
                af->pCompensateChunk        = NULL;
                af->pCompensateFadeType     = NULL;
                af->pActive                 = NULL;
                af->pPlayPosition           = NULL;
                af->pNoteOn                 = NULL;
                af->pLength                 = NULL;
                af->pActualLength           = NULL;
                af->pStatus                 = NULL;
                af->pMesh                   = NULL;

                for (size_t j=0; j < meta::sampler_metadata::TRACKS_MAX; ++j)
                {
                    af->fGains[j]               = 1.0f;
                    af->pGains[j]               = NULL;
                }

                vActive[i]                  = NULL;
            }

            // Assert
            lsp_assert(ptr <= tail);

            // Create additional objects: tasks for file loading
            lsp_trace("Create tasks");
            for (size_t i=0; i<files; ++i)
            {
                afile_t  *af        = &vFiles[i];

                // Create loader
                af->pLoader         = new AFLoader(this, af);
                if (af->pLoader == NULL)
                {
                    destroy_state();
                    return false;
                }

                // Create renderer
                af->pRenderer       = new AFRenderer(this, af);
                if (af->pRenderer == NULL)
                {
                    destroy_state();
                    return false;
                }
            }

            // Initialize channels
            lsp_trace("Initialize channels");
            for (size_t i=0; i<nChannels; ++i)
            {
                if (!vChannels[i].init(nFiles, meta::sampler_metadata::PLAYBACKS_MAX))
                {
                    destroy_state();
                    return false;
                }
            }

            // Initialize toggle
            sListen.init();

            return true;
        }

        void sampler_kernel::bind(plug::IPort **ports, size_t & port_id, bool dynamics)
        {
            lsp_trace("Binding listen toggle...");
            BIND_PORT(pListen);

            if (dynamics)
            {
                lsp_trace("Binding dynamics and drifting...");
                BIND_PORT(pDynamics);
                BIND_PORT(pDrift);
            }

            lsp_trace("Skipping sample selector port...");
            SKIP_PORT("Sample selector");

            // Iterate each file
            for (size_t i=0; i<nFiles; ++i)
            {
                lsp_trace("Binding sample %d", int(i));

                afile_t *af             = &vFiles[i];
                // Allocate files
                BIND_PORT(af->pFile);
                BIND_PORT(af->pPitch);
                BIND_PORT(af->pStretchOn);
                BIND_PORT(af->pStretch);
                BIND_PORT(af->pStretchStart);
                BIND_PORT(af->pStretchEnd);
                BIND_PORT(af->pStretchChunk);
                BIND_PORT(af->pStretchFade);
                BIND_PORT(af->pStretchFadeType);
                BIND_PORT(af->pLoopOn);
                BIND_PORT(af->pLoopMode);
                BIND_PORT(af->pLoopStart);
                BIND_PORT(af->pLoopEnd);
                BIND_PORT(af->pLoopFade);
                BIND_PORT(af->pLoopFadeType);
                BIND_PORT(af->pHeadCut);
                BIND_PORT(af->pTailCut);
                BIND_PORT(af->pFadeIn);
                BIND_PORT(af->pFadeOut);
                BIND_PORT(af->pMakeup);
                BIND_PORT(af->pVelocity);
                BIND_PORT(af->pPreDelay);
                BIND_PORT(af->pOn);
                BIND_PORT(af->pListen);
                BIND_PORT(af->pPreReverse);
                BIND_PORT(af->pPostReverse);
                BIND_PORT(af->pCompensate);
                BIND_PORT(af->pCompensateFade);
                BIND_PORT(af->pCompensateChunk);
                BIND_PORT(af->pCompensateFadeType);

                for (size_t j=0; j<nChannels; ++j)
                    BIND_PORT(af->pGains[j]);

                BIND_PORT(af->pActive);
                BIND_PORT(af->pPlayPosition);
                BIND_PORT(af->pNoteOn);
                BIND_PORT(af->pLength);
                BIND_PORT(af->pActualLength);
                BIND_PORT(af->pStatus);
                BIND_PORT(af->pMesh);
            }

            // Initialize randomizer
            sRandom.init();

            lsp_trace("Init OK");
        }

        void sampler_kernel::bind_activity(plug::IPort **ports, size_t & port_id)
        {
            lsp_trace("Binding activity...");
            BIND_PORT(pActivity);
        }

        void sampler_kernel::destroy_sample(dspu::Sample * &sample)
        {
            if (sample == NULL)
                return;

            // Free user data associated with sample
            render_params_t *params = static_cast<render_params_t *>(sample->user_data());
            if (params != NULL)
            {
                delete params;
                sample->set_user_data(NULL);
            }

            // Destroy the sample
            sample->destroy();
            delete sample;
            lsp_trace("Destroyed sample %p", sample);
            sample  = NULL;
        }

        void sampler_kernel::destroy_afile(afile_t *af)
        {
            af->sListen.destroy();
            af->sNoteOn.destroy();
            for (size_t i=0; i<4; ++i)
            {
                af->vPlayback[i].destroy();
                af->vListen[i].destroy();
            }

            // Delete audio file loader
            if (af->pLoader != NULL)
            {
                delete af->pLoader;
                af->pLoader = NULL;
            }

            // Delete audio file renderer
            if (af->pRenderer != NULL)
            {
                delete af->pRenderer;
                af->pRenderer = NULL;
            }

            // Destroy all sample-related data
            unload_afile(af);

            // Active sample is bound to the sampler, controlled by GC
            af->pActive     = NULL;
        }

        void sampler_kernel::destroy_samples(dspu::Sample *gc_list)
        {
            // Iterate over the list and destroy each sample in the list
            while (gc_list != NULL)
            {
                dspu::Sample *next = gc_list->gc_next();
                destroy_sample(gc_list);
                gc_list = next;
            }
        }

        void sampler_kernel::perform_gc()
        {
            dspu::Sample *gc_list = lsp::atomic_swap(&pGCList, NULL);
            lsp_trace("gc_list = %p", gc_list);
            destroy_samples(gc_list);
        }

        void sampler_kernel::destroy_state()
        {
            // Perform garbage collection for each channel
            for (size_t i=0; i<nChannels; ++i)
            {
                dspu::SamplePlayer *sp = &vChannels[i];
                dspu::Sample *gc_list = sp->destroy(false);
                destroy_samples(gc_list);
            }

            // Destroy audio files
            if (vFiles != NULL)
            {
                for (size_t i=0; i<nFiles;++i)
                    destroy_afile(&vFiles[i]);
            }

            // Perform pending gabrage collection
            perform_gc();

            // Drop all preallocated data
            free_aligned(pData);

            // Foget variables
            vFiles          = NULL;
            vActive         = NULL;
            vBuffer         = NULL;
            pExecutor       = NULL;
            nFiles          = 0;
            nChannels       = 0;
            bReorder        = false;
            bBypass         = false;

            pDynamics       = NULL;
            pDrift          = NULL;
        }

        void sampler_kernel::destroy()
        {
            destroy_state();
        }

        template <class T>
        void sampler_kernel::commit_value(uint32_t & counter, T & field, plug::IPort *port)
        {
            const T temp = port->value();
            if (temp != field)
            {
                field       = temp;
                ++counter;
            }
        }

        void sampler_kernel::commit_value(uint32_t & counter, bool & field, plug::IPort *port)
        {
            const bool temp = port->value() >= 0.5f;
            if (temp != field)
            {
                field       = temp;
                ++counter;
            }
        }

        dspu::sample_loop_t sampler_kernel::decode_loop_mode(plug::IPort *on, plug::IPort *mode)
        {
            if ((on == NULL) || (on->value() < 0.5f))
                return dspu::SAMPLE_LOOP_NONE;
            if (mode == NULL)
                return dspu::SAMPLE_LOOP_DIRECT;

            switch (int(mode->value()))
            {
                case LOOP_DIRECT:           return dspu::SAMPLE_LOOP_DIRECT;
                case LOOP_REVERSE:          return dspu::SAMPLE_LOOP_REVERSE;
                case LOOP_DIRECT_HALF_PP:   return dspu::SAMPLE_LOOP_DIRECT_HALF_PP;
                case LOOP_REVERSE_HALF_PP:  return dspu::SAMPLE_LOOP_REVERSE_HALF_PP;
                case LOOP_DIRECT_FULL_PP:   return dspu::SAMPLE_LOOP_DIRECT_FULL_PP;
                case LOOP_REVERSE_FULL_PP:  return dspu::SAMPLE_LOOP_REVERSE_FULL_PP;
                case LOOP_DIRECT_SMART_PP:  return dspu::SAMPLE_LOOP_DIRECT_SMART_PP;
                case LOOP_REVERSE_SMART_PP: return dspu::SAMPLE_LOOP_REVERSE_SMART_PP;
                default: break;
            }

            return dspu::SAMPLE_LOOP_DIRECT;
        }

        void sampler_kernel::update_settings()
        {
            // Process listen toggle
            if (pListen != NULL)
                sListen.submit(pListen->value());

            // Update note and octave
//            lsp_trace("Initializing samples...");

            // Iterate all samples
            for (size_t i=0; i<nFiles; ++i)
            {
                afile_t *af         = &vFiles[i];

                // On/off switch
                bool on             = (af->pOn->value() >= 0.5f);
                if (af->bOn != on)
                {
                    af->bOn             = on;
                    bReorder            = true;
                }

                // Pre-delay gain
                af->fPreDelay       = af->pPreDelay->value();

                // Listen trigger
    //            lsp_trace("submit listen%d = %f", int(i), af->pListen->getValue());
                af->sListen.submit(af->pListen->value());
    //            lsp_trace("listen[%d].pending = %s", int(i), (af->sListen.pending()) ? "true" : "false");

                // Makeup gain + mix gain
                af->fMakeup         = (af->pMakeup != NULL) ? af->pMakeup->value() : 1.0f;
                if (nChannels == 1)
                    af->fGains[0]       = af->pGains[0]->value();
                else if (nChannels == 2)
                {
                    af->fGains[0]       = (100.0f - af->pGains[0]->value()) * 0.005f;
                    af->fGains[1]       = (af->pGains[1]->value() + 100.0f) * 0.005f;
                }
                else
                {
                    for (size_t j=0; j<nChannels; ++j)
                        af->fGains[j]       = af->pGains[j]->value();
                }
    //            #ifdef LSP_TRACE
    //                for (size_t j=0; j<nChannels; ++j)
    //                    lsp_trace("gains[%d,%d] = %f", int(i), int(j), af->fGains[j]);
    //            #endif

                // Update velocity
                float value     = af->pVelocity->value();
                if (value != af->fVelocity)
                {
                    af->fVelocity   = value;
                    bReorder        = true;
                }

                // Update sample parameters
                size_t upd_req = af->nUpdateReq;
                commit_value(af->nUpdateReq, af->fPitch, af->pPitch);
                commit_value(af->nUpdateReq, af->bStretchOn, af->pStretchOn);
                commit_value(af->nUpdateReq, af->fStretch, af->pStretch);
                commit_value(af->nUpdateReq, af->fStretchStart, af->pStretchStart);
                commit_value(af->nUpdateReq, af->fStretchEnd, af->pStretchEnd);
                commit_value(af->nUpdateReq, af->fStretchChunk, af->pStretchChunk);
                commit_value(af->nUpdateReq, af->fStretchFade, af->pStretchFade);
                commit_value(af->nUpdateReq, af->nStretchFadeType, af->pStretchFadeType);
                commit_value(af->nUpdateReq, af->fHeadCut, af->pHeadCut);
                commit_value(af->nUpdateReq, af->fTailCut, af->pTailCut);
                commit_value(af->nUpdateReq, af->fFadeIn, af->pFadeIn);
                commit_value(af->nUpdateReq, af->fFadeOut, af->pFadeOut);
                commit_value(af->nUpdateReq, af->bPreReverse, af->pPreReverse);
                commit_value(af->nUpdateReq, af->bPostReverse, af->pPostReverse);
                commit_value(af->nUpdateReq, af->bCompensate, af->pCompensate);
                commit_value(af->nUpdateReq, af->fCompensateFade, af->pCompensateFade);
                commit_value(af->nUpdateReq, af->fCompensateChunk, af->pCompensateChunk);
                commit_value(af->nUpdateReq, af->nCompensateFadeType, af->pCompensateFadeType);

                // Update loop parameters
                uint32_t loop_update = 0;
                dspu::sample_loop_t loop_mode = decode_loop_mode(af->pLoopOn, af->pLoopMode);
                if (af->enLoopMode != loop_mode)
                {
                    af->enLoopMode = loop_mode;
                    ++loop_update;
                }
                commit_value(loop_update, af->fLoopStart, af->pLoopStart);
                commit_value(loop_update, af->fLoopEnd, af->pLoopEnd);
                commit_value(loop_update, af->fLoopFade, af->pLoopFade);
                commit_value(loop_update, af->nLoopFadeType, af->pLoopFadeType);

                if ((loop_update > 0) || (upd_req != af->nUpdateReq))
                    cancel_sample(af, 0);
            }

            // Get humanisation parameters
            fDynamics       = (pDynamics != NULL) ? pDynamics->value() * 0.01f : 0.0f; // fDynamics = 0..1.0
            fDrift          = (pDrift != NULL)    ? pDrift->value() : 0.0f;
        }

        void sampler_kernel::sync_samples_with_ui()
        {
            // Iterate all samples
            for (size_t i=0; i<nFiles; ++i)
            {
                afile_t *af         = &vFiles[i];
                af->bSync           = true;
            }
        }

        void sampler_kernel::update_sample_rate(long sr)
        {
            // Store new sample rate
            nSampleRate     = sr;

            // Update activity counter
            sActivity.init(sr);

            for (size_t i=0; i<nFiles; ++i)
                vFiles[i].sNoteOn.init(sr);
        }

        void sampler_kernel::unload_afile(afile_t *af)
        {
            // Destroy original sample if present
            destroy_sample(af->pOriginal);
            destroy_sample(af->pProcessed);

            // Destroy pointer to thumbnails
            if (af->vThumbs[0])
            {
                free(af->vThumbs[0]);
                for (size_t i=0; i<meta::sampler_metadata::TRACKS_MAX; ++i)
                    af->vThumbs[i]              = NULL;
            }
        }

        status_t sampler_kernel::load_file(afile_t *file)
        {
            // Load sample
            lsp_trace("file = %p", file);

            // Validate arguments
            if ((file == NULL) || (file->pFile == NULL))
                return STATUS_UNKNOWN_ERR;

            unload_afile(file);

            // Get path
            plug::path_t *path      = file->pFile->buffer<plug::path_t>();
            if (path == NULL)
                return STATUS_UNKNOWN_ERR;

            // Get file name
            const char *fname   = path->path();
            if (strlen(fname) <= 0)
                return STATUS_UNSPECIFIED;

            // Load audio file
            dspu::Sample *source    = new dspu::Sample();
            if (source == NULL)
                return STATUS_NO_MEM;
            lsp_trace("Allocated sample %p", source);
            lsp_finally { destroy_sample(source); };

            // Load sample
            status_t status = source->load_ext(fname, meta::sampler_metadata::SAMPLE_LENGTH_MAX * 0.001f);
            if (status != STATUS_OK)
            {
                lsp_trace("load failed: status=%d (%s)", status, get_status(status));
                return status;
            }
            const size_t channels   = lsp_min(nChannels, source->channels());
            if (!source->set_channels(channels))
            {
                lsp_trace("failed to resize source sample to %d channels", int(channels));
                return status;
            }

            // Initialize thumbnails
            float *thumbs           = static_cast<float *>(malloc(sizeof(float) * channels * meta::sampler_metadata::MESH_SIZE));
            if (thumbs == NULL)
                return STATUS_NO_MEM;

            for (size_t i=0; i<channels; ++i)
            {
                file->vThumbs[i]        = thumbs;
                thumbs                 += meta::sampler_metadata::MESH_SIZE;
            }

            // Commit the result
            lsp_trace("file successfully loaded: %s", fname);
            lsp::swap(file->pOriginal, source);

            return STATUS_OK;
        }

        status_t sampler_kernel::render_sample(afile_t *af)
        {
            status_t res;

            // Validate arguments
            if (af == NULL)
                return STATUS_UNKNOWN_ERR;

            // Get maximum sample count
            dspu::Sample *src       = af->pOriginal;
            if (src == NULL)
                return STATUS_UNSPECIFIED;

            // Copy data of original sample to temporary sample and perform resampling
            dspu::Sample temp;
            size_t channels         = lsp_min(nChannels, src->channels());
            size_t sample_rate_dst  = nSampleRate * dspu::semitones_to_frequency_shift(-af->fPitch);
            if (temp.copy(src) != STATUS_OK)
            {
                lsp_warn("Error copying source sample");
                return STATUS_NO_MEM;
            }
            if (temp.resample(sample_rate_dst) != STATUS_OK)
            {
                lsp_warn("Error resampling source sample");
                return STATUS_NO_MEM;
            }
            if (af->bPreReverse)
                temp.reverse();

            if (af->bCompensate)
            {
                size_t chunk_size       = dspu::millis_to_samples(nSampleRate, af->fCompensateChunk);
                dspu::sample_crossfade_t fade_type  = (af->nCompensateFadeType == XFADE_LINEAR) ?
                    dspu::SAMPLE_CROSSFADE_LINEAR :
                    dspu::SAMPLE_CROSSFADE_CONST_POWER;
                float crossfade         = lsp_limit(af->fCompensateFade * 0.01f, 0.0f, 1.0f);

                if ((res = temp.stretch(src->length(), chunk_size, fade_type, crossfade)) != STATUS_OK)
                    return res;
            }

            // Determine the normalizing factor
            float abs_max           = 0.0f;
            for (size_t i=0; i<channels; ++i)
            {
                // Determine the maximum amplitude
                float a_max             = dsp::abs_max(temp.channel(i), temp.length());
                abs_max                 = lsp_max(abs_max, a_max);
            }
            float norming           = (abs_max != 0.0f) ? 1.0f / abs_max : 1.0f;
            af->fLength             = dspu::samples_to_millis(nSampleRate, temp.length());

            // Allocate target sample
            dspu::Sample *out   = new dspu::Sample();
            if (out == NULL)
                return STATUS_NO_MEM;
            lsp_trace("Allocated sample %p", out);
            lsp_finally { destroy_sample(out); };
            out->set_sample_rate(nSampleRate);

            // Allocate user data and bind to the sample
            render_params_t *rp     = new render_params_t;
            if (rp == NULL)
                return STATUS_NO_MEM;
            rp->nHeadCut            = 0;
            rp->nTailCut            = 0;
            rp->nCutLength          = 0;
            rp->nStretchDelta       = 0;
            rp->nStretchStart       = 0;
            rp->nStretchEnd         = 0;
            out->set_user_data(rp);

            // Perform stretch of the sample
            rp->nStretchDelta       = (af->bStretchOn) ? dspu::millis_to_samples(nSampleRate, af->fStretch) : 0.0f;
            if (rp->nStretchDelta != 0)
            {
                rp->nStretchStart       = lsp_limit(dspu::millis_to_samples(nSampleRate, af->fStretchStart), 0, temp.length());
                rp->nStretchEnd         = lsp_limit(dspu::millis_to_samples(nSampleRate, af->fStretchEnd), 0, temp.length());
                if (rp->nStretchStart <= rp->nStretchEnd)
                {
                    ssize_t s_length        = lsp_max(rp->nStretchEnd + rp->nStretchDelta - rp->nStretchStart, 0);
                    size_t chunk_size       = dspu::millis_to_samples(nSampleRate, af->fStretchChunk);
                    dspu::sample_crossfade_t fade_type  = (af->nStretchFadeType == XFADE_LINEAR) ?
                        dspu::SAMPLE_CROSSFADE_LINEAR :
                        dspu::SAMPLE_CROSSFADE_CONST_POWER;
                    float crossfade         = lsp_limit(af->fStretchFade * 0.01f, 0.0f, 1.0f);

                    // Perform stretch only when it is possible, do not report errors if stretch didn't succeed
                    res = temp.stretch(s_length, chunk_size, fade_type, crossfade, rp->nStretchStart, rp->nStretchEnd);
                    if (res != STATUS_OK)
                    {
                        lsp_trace("Failed to stretch sample: %d", int(res));
                        rp->nStretchDelta       = 0;
                    }
                }
                else
                {
                    rp->nStretchStart   = -1;
                    rp->nStretchEnd     = -1;
                }
            }

            // Compute the tail and head cut positions
            rp->nLength         = temp.length();
            af->fActualLength   = dspu::samples_to_millis(nSampleRate, rp->nLength);
            rp->nHeadCut        = lsp_limit(dspu::millis_to_samples(nSampleRate, af->fHeadCut), 0, rp->nLength);
            rp->nTailCut        = lsp_limit(dspu::millis_to_samples(nSampleRate, af->fTailCut), 0, rp->nLength);

            // Apply the fade-in and fade-out
            ssize_t fade_in     = dspu::millis_to_samples(nSampleRate, af->fFadeIn);
            ssize_t fade_out    = dspu::millis_to_samples(nSampleRate, af->fFadeOut);
            for (size_t j=0; j<channels; ++j)
            {
                float *dst          = temp.channel(j);

                dspu::fade_in(&dst[rp->nHeadCut], &dst[rp->nHeadCut], fade_in, rp->nLength - rp->nHeadCut);
                dspu::fade_out(dst, dst, fade_out, rp->nLength - rp->nTailCut);
            }

            // Render the thumbnails
            for (size_t j=0; j<channels; ++j)
            {
                const float *src    = temp.channel(j);
                float *dst          = af->vThumbs[j];
                size_t len          = temp.length();

                for (size_t k=0; k<meta::sampler_metadata::MESH_SIZE; ++k)
                {
                    size_t first    = (k * len) / meta::sampler_metadata::MESH_SIZE;
                    size_t last     = ((k + 1) * len) / meta::sampler_metadata::MESH_SIZE;
                    if (first < last)
                        dst[k]          = dsp::abs_max(&src[first], last - first);
                    else if (first < len)
                        dst[k]          = fabs(src[first]);
                    else
                        dst[k]          = 0.0f;
                }

                // Normalize graph if possible
                if (norming != 1.0f)
                    dsp::mul_k2(dst, norming, meta::sampler_metadata::MESH_SIZE);
            }

            // Perform the head and tail cut operations
            rp->nCutLength      = lsp_max(rp->nLength - rp->nTailCut - rp->nHeadCut, 0);

            // Initialize target sample
            if (!out->resize(channels, rp->nCutLength, rp->nCutLength))
            {
                lsp_warn("Error initializing playback sample");
                return STATUS_NO_MEM;
            }

            // Apply head cut and tail cut
            for (size_t j=0; j<channels; ++j)
            {
                const float *src    = temp.channel(j);
                float *dst          = out->channel(j);
                dsp::copy(dst, &src[rp->nHeadCut], rp->nCutLength);
            }

            // Commit the new sample to the processed
            rp  = static_cast<render_params_t *>(out->set_user_data(rp));
            lsp::swap(out, af->pProcessed);

            return STATUS_OK;
        }

        ssize_t sampler_kernel::compute_loop_point(const dspu::Sample *s, size_t position)
        {
            ssize_t pos         = dspu::millis_to_samples(s->sample_rate(), position);
            const render_params_t *rp = static_cast<render_params_t *>(s->user_data());
            if (rp != NULL)
            {
                pos                 = lsp_limit(pos, 0, rp->nLength);
                pos                -= rp->nHeadCut;
                if (pos >= rp->nLength)
                    pos                 = -1;
            }

            return pos;
        }

        void sampler_kernel::cancel_sample(afile_t *af, size_t delay)
        {
            size_t fadeout  = dspu::millis_to_samples(nSampleRate, fFadeout);

            for (size_t i=0; i<nChannels; ++i)
            {
                dspu::SamplePlayer *p = &vChannels[i];
                for (size_t j=0; j<nChannels; ++j)
                    p->cancel_all(af->nID, j, fadeout, delay);
            }

            for (size_t i=0; i<4; ++i)
            {
                af->vListen[i].clear();
                af->vPlayback[i].clear();
            }
        }

        void sampler_kernel::play_sample(afile_t *af, float gain, size_t delay, play_mode_t mode)
        {
            lsp_trace("id=%d, gain=%f, delay=%d", int(af->nID), gain, int(delay));

            // Obtain the sample that will be used for playback
            dspu::Sample *s = vChannels[0].get(af->nID);
            if (s == NULL)
                return;

            // Scale the final output gain
            dspu::PlaySettings ps;
            ssize_t loop_start  = compute_loop_point(s, af->fLoopStart);
            ssize_t loop_end    = compute_loop_point(s, af->fLoopEnd);
            if (loop_end < loop_start)
                lsp::swap(loop_end, loop_start);

            ps.set_sample_id(af->nID);
            if ((loop_start >= 0) && (loop_end >= 0))
                ps.set_loop_range(af->enLoopMode, loop_start, loop_end);
            ps.set_loop_xfade(
                (af->nLoopFadeType == XFADE_LINEAR) ? dspu::SAMPLE_CROSSFADE_LINEAR : dspu::SAMPLE_CROSSFADE_CONST_POWER,
                dspu::millis_to_samples(nSampleRate, af->fLoopFade));
            ps.set_delay(delay);
            ps.set_start((af->bPostReverse) ? s->length() : 0, af->bPostReverse);

            dspu::Playback *vpb = (mode == PLAY_FILE) ? af->vListen :
                                  (mode == PLAY_INSTRUMENT) ? vListen :
                                  af->vPlayback;
            gain               *= af->fMakeup;
            if (nChannels == 1)
            {
                lsp_trace("channels[%d].play(%d, %d, %f, %d)", int(0), int(af->nID), int(0), gain * af->fGains[0], int(delay));
                ps.set_sample_channel(0);
                ps.set_volume(gain * af->fGains[0]);
                vpb[0].set(vChannels[0].play(&ps));
                vpb[1].clear();
                vpb[2].clear();
                vpb[3].clear();
            }
            else // if (nChannels == 2)
            {
                size_t pb_id = 0;
                for (size_t i=0; i<2; ++i)
                {
                    size_t j=i^1; // j = (i + 1) % 2
                    ps.set_sample_channel(i % s->channels());

                    lsp_trace("channels[%d].play(%d, %d, %f, %d)", int(i), int(af->nID), int(i), gain * af->fGains[i], int(delay));
                    ps.set_volume(gain * af->fGains[i]);
                    vpb[pb_id++].set(vChannels[i].play(&ps));
                    lsp_trace("channels[%d].play(%d, %d, %f, %d)", int(j), int(i), int(af->nID), gain * (1.0f - af->fGains[i]), int(delay));
                    ps.set_volume(gain * (1.0f - af->fGains[i]));
                    vpb[pb_id++].set(vChannels[j].play(&ps));
                }
            }
        }

        void sampler_kernel::stop_listen_file(afile_t *af, bool force)
        {
//            lsp_trace("id=%d, force=%s", int(af->nID), (force) ? "true" : "false");

            if (force)
            {
                size_t fadeout  = dspu::millis_to_samples(nSampleRate, fFadeout);
                for (size_t i=0; i<4; ++i)
                    af->vListen[i].cancel(fadeout, 0);
            }
            else
            {
                for (size_t i=0; i<4; ++i)
                    af->vListen[i].stop();
            }
        }

        void sampler_kernel::start_listen_file(afile_t *af, float gain)
        {
            play_sample(af, gain, 0, PLAY_FILE);
        }

        void sampler_kernel::start_listen_instrument(float velocity, float gain)
        {
            // Obtain the active file and
            afile_t *af     = select_active_sample(velocity);
            if (af != NULL)
                play_sample(af, gain, 0, PLAY_INSTRUMENT);
        }

        void sampler_kernel::stop_listen_instrument(bool force)
        {
//            lsp_trace("force=%s", (force) ? "true" : "false");
            if (force)
            {
                size_t fadeout  = dspu::millis_to_samples(nSampleRate, fFadeout);
                for (size_t i=0; i<4; ++i)
                    vListen[i].cancel(fadeout, 0);
            }
            else
                for (size_t i=0; i<4; ++i)
                    vListen[i].stop(0);
            {
            }
        }

        sampler_kernel::afile_t *sampler_kernel::select_active_sample(float velocity)
        {
            if (nActive <= 0)
                return NULL;

            // Binary search of sample
            lsp_trace("normalized velocity = %f", velocity);
            ssize_t f_first = 0, f_last = nActive-1;
            while (f_last > f_first)
            {
                ssize_t f_mid = (f_last + f_first) >> 1;
                if (velocity <= vActive[f_mid]->fVelocity)
                    f_last  = f_mid;
                else
                    f_first = f_mid + 1;
            }

            f_last      = lsp_limit(f_last, 0, ssize_t(nActive) - 1);
            return vActive[f_last];
        }

        void sampler_kernel::trigger_on(size_t timestamp, float level)
        {
            // Get the file and ajdust gain
            float velocity  = level * 100.0f;       // Compute velocity in percents
            afile_t *af     = select_active_sample(velocity);
            if (af == NULL)
                return;

            size_t delay    = dspu::millis_to_samples(nSampleRate, af->fPreDelay) + timestamp;
            lsp_trace("af->id=%d, af->velocity=%.3f", int(af->nID), af->fVelocity);

            // Apply changes to all ports
            if (af->fVelocity > 0.0f)
            {
                // Apply 'Humanisation' parameters
                level       = velocity * ((1.0f - fDynamics*0.5) + fDynamics * sRandom.random(dspu::RND_EXP)) / af->fVelocity;
                delay      += dspu::millis_to_samples(nSampleRate, fDrift) * sRandom.random(dspu::RND_EXP);

                // Play sample
                play_sample(af, level, delay, PLAY_NOTE);

                // Trigger the note On indicator
                af->sNoteOn.blink();
                sActivity.blink();
            }
        }

        void sampler_kernel::trigger_off(size_t timestamp, bool note_off)
        {
            lsp_trace("timestamp=%d, note_off=%s",
                int(timestamp),
                (note_off) ? "true" : "false");

            // Stop active playback and listen events
            for (size_t i=0; i<nFiles; ++i)
            {
                afile_t *af = &vFiles[i];
                if ((note_off) || (af->enLoopMode != dspu::SAMPLE_LOOP_NONE))
                {
                    for (size_t j=0; j<4; ++j)
                        af->vPlayback[j].stop(timestamp);
                }
            }
        }

        void sampler_kernel::trigger_cancel(size_t timestamp)
        {
            lsp_trace("timestamp=%d", int(timestamp));

            // Cancel active playback
            for (size_t i=0; i<nFiles; ++i)
                cancel_sample(&vFiles[i], timestamp);
        }

        void sampler_kernel::process_file_load_requests()
        {
            // Process file load requests
            for (size_t i=0; i<nFiles; ++i)
            {
                // Get descriptor
                afile_t *af             = &vFiles[i];
                if (af->pFile == NULL)
                    continue;

                // Do nothing if rendering is in progress
                if (!af->pRenderer->idle())
                    continue;

                // Get path
                plug::path_t *path = af->pFile->buffer<plug::path_t>();
                if (path == NULL)
                    continue;

                // If there is new load request and loader is idle, then wake up the loader
                if ((path->pending()) && (af->pLoader->idle()))
                {
                    // Try to submit task
                    if (pExecutor->submit(af->pLoader))
                    {
                        ++af->nUpdateReq;
                        af->nStatus     = STATUS_LOADING;
                        lsp_trace("successfully submitted loader task");
                        path->accept();
                    }
                }
                else if ((path->accepted()) && (af->pLoader->completed()))
                {
                    // Commit the result
                    af->nStatus     = af->pLoader->code();
                    af->fLength     = (af->nStatus == STATUS_OK) ? af->pOriginal->duration() * 1000.0f : 0.0f;

                    // Trigger the sample for update and the state for reorder
                    ++af->nUpdateReq;
                    bReorder        = true;

                    // Now we can surely commit changes and reset task state
                    path->commit();
                    af->pLoader->reset();
                }
            }
        }

        void sampler_kernel::process_file_render_requests()
        {
            for (size_t i=0; i<nFiles; ++i)
            {
                // Get descriptor
                afile_t *af         = &vFiles[i];
                if (af->pFile == NULL)
                    continue;

                // Do nothing if loader is in progress
                if (!af->pLoader->idle())
                    continue;

                // Get path and check task state
                if ((af->nUpdateReq != af->nUpdateResp) && (af->pRenderer->idle()))
                {
                    if (af->pOriginal == NULL)
                    {
                        af->nUpdateResp     = af->nUpdateReq;
                        af->pProcessed      = NULL;

                        // Unbind sample for all channels
                        for (size_t j=0; j<nChannels; ++j)
                            vChannels[j].unbind(af->nID);

                        af->bSync           = true;
                    }
                    else if (pExecutor->submit(af->pRenderer))
                    {
                        // Try to submit task
                        af->nUpdateResp     = af->nUpdateReq;
                        lsp_trace("successfully submitted renderer task");
                    }
                }
                else if (af->pRenderer->completed())
                {
                    // Canel all current playbacks for the audio file
                    cancel_sample(af, 0);

                    // Commit changes if there is no more pending tasks
                    if (af->nUpdateReq == af->nUpdateResp)
                    {
                        // Bind sample for all channels
                        for (size_t j=0; j<nChannels; ++j)
                            vChannels[j].bind(af->nID, af->pProcessed);

                        // The sample is now under the garbage control inside of the sample player
                        af->pProcessed      = NULL;
                    }

                    af->pRenderer->reset();
                    af->bSync           = true;
                }
            }
        }

        void sampler_kernel::process_gc_tasks()
        {
            if (sGCTask.completed())
                sGCTask.reset();

            if (sGCTask.idle())
            {
                // Obtain the list of samples for destroy
                if (pGCList == NULL)
                {
                    for (size_t i=0; i<meta::sampler_metadata::TRACKS_MAX; ++i)
                        if ((pGCList = vChannels[i].gc()) != NULL)
                            break;
                }

                if (pGCList != NULL)
                    pExecutor->submit(&sGCTask);
            }
        }

        void sampler_kernel::process_listen_events()
        {
            if (sListen.pending())
            {
                stop_listen_instrument(true);
                start_listen_instrument(0.5f, 1.0f);
                sListen.commit();
            }
            else if (sListen.off())
                stop_listen_instrument(false);

            for (size_t i=0; i<nFiles; ++i)
            {
                // Get descriptor
                afile_t *af         = &vFiles[i];
                if (af->pFile == NULL)
                    continue;

                // Play audio file
                if (af->sListen.pending())
                {
                    stop_listen_file(af, true);
                    start_listen_file(af, 1.0f);
                    af->sNoteOn.blink();
                    af->sListen.commit();
                }
                else if (af->sListen.off())
                    stop_listen_file(af, false);
            }
        }

        void sampler_kernel::reorder_samples()
        {
            if (!bReorder)
                return;
            bReorder = false;

            lsp_trace("Reordering active files");

            // Compute the list of active files
            nActive     = 0;
            for (size_t i=0; i<nFiles; ++i)
            {
                if (!vFiles[i].bOn)
                    continue;
                if (vFiles[i].pOriginal == NULL)
                    continue;

                lsp_trace("file %d is active", int(nActive));
                vActive[nActive++]  = &vFiles[i];
            }

            // Sort the list of active files
            if (nActive > 1)
            {
                for (size_t i=0; i<(nActive-1); ++i)
                    for (size_t j=i+1; j<nActive; ++j)
                        if (vActive[i]->fVelocity > vActive[j]->fVelocity)
                            lsp::swap(vActive[i], vActive[j]);
            }

            #ifdef LSP_TRACE
                for (size_t i=0; i<nActive; ++i)
                    lsp_trace("active file #%d: velocity=%.3f", int(vActive[i]->nID), vActive[i]->fVelocity);
            #endif /* LSP_TRACE */
        }

        void sampler_kernel::play_samples(float **outs, const float **ins, size_t samples)
        {
            if (ins != NULL)
            {
                for (size_t i=0; i<nChannels; ++i)
                    vChannels[i].process(outs[i], ins[i], samples);
            }
            else
            {
                for (size_t i=0; i<nChannels; ++i)
                    vChannels[i].process(outs[i], NULL, samples);
            }
        }

        void sampler_kernel::output_parameters(size_t samples)
        {
            // Update activity led output
            if (pActivity != NULL)
                pActivity->set_value(sActivity.process(samples));

            for (size_t i=0; i<nFiles; ++i)
            {
                afile_t *af         = &vFiles[i];

                // Output information about the file
                af->pLength->set_value(af->fLength);
                af->pActualLength->set_value(af->fActualLength);
                af->pStatus->set_value(af->nStatus);

                // Output information about the activity
                af->pNoteOn->set_value(af->sNoteOn.process(samples));

                // Get file sample
                dspu::Sample *active    = vChannels[0].get(af->nID);
                size_t channels         = (active != NULL) ? active->channels() : 0;
                channels                = lsp_min(channels, nChannels);

                // Output activity flag
                af->pActive->set_value(((af->bOn) && (channels > 0)) ? 1.0f : 0.0f);
                af->pPlayPosition->set_value(compute_play_position(af));

                // Store file thumbnails to mesh
                plug::mesh_t *mesh  = reinterpret_cast<plug::mesh_t *>(af->pMesh->buffer());
                if ((mesh == NULL) || (!mesh->isEmpty()) || (!af->bSync) || (!af->pLoader->idle()) || (!af->pRenderer->idle()))
                    continue;

                if ((channels > 0) && (af->vThumbs[0] != NULL))
                {
                    // Copy thumbnails
                    for (size_t j=0; j<channels; ++j)
                        dsp::copy(mesh->pvData[j], af->vThumbs[j], meta::sampler_metadata::MESH_SIZE);

                    mesh->data(channels, meta::sampler_metadata::MESH_SIZE);
                }
                else
                    mesh->data(0, 0);

                af->bSync           = false;
            }
        }

        void sampler_kernel::process(float **outs, const float **ins, size_t samples)
        {
            process_file_load_requests();
            process_file_render_requests();
            process_gc_tasks();
            reorder_samples();
            process_listen_events();
            play_samples(outs, ins, samples);
            output_parameters(samples);
        }

        float sampler_kernel::compute_play_position(const afile_t *f)
        {
            const dspu::Playback *pb = &f->vListen[0];
            if (!pb->valid())
                pb = &vListen[0];
            if (!pb->valid())
                pb = &f->vPlayback[0];
            if (!pb->valid())
                return meta::sampler_metadata::SAMPLE_PLAYBACK_MIN;

            ssize_t position = pb->position();
            if (position < 0)
                return meta::sampler_metadata::SAMPLE_PLAYBACK_MIN;

            // We need to translate the current play position of the processed sample
            // into the current play position of thumbnail sample
            const dspu::Sample *s = pb->sample();
            const render_params_t *rp = static_cast<render_params_t *>(s->user_data());
            if (rp != NULL)
                position       += rp->nHeadCut;
            float time      = dspu::samples_to_millis(s->sample_rate(), position);

//            lsp_trace("play position %d: %d / %d -> %.3f", int(pb->id()), int(position), int(s->length()), time);

            return time;
        }

        void sampler_kernel::dump_afile(dspu::IStateDumper *v, const afile_t *f) const
        {
            v->write("nID", f->nID);
            v->write_object("pLoader", f->pLoader);
            v->write_object("pRenderer", f->pRenderer);
            v->write_object("sListen", &f->sListen);
            v->write_object("sNoteOn", &f->sNoteOn);
            v->write_object_array("vPlayback", f->vPlayback, 4);
            v->write_object_array("vListen", f->vListen, 4);
            v->write_object("pOriginal", f->pOriginal);
            v->write_object("pProcessed", f->pProcessed);
            v->write("vThumbs", f->vThumbs);

            v->write("nUpdateReq", f->nUpdateReq);
            v->write("nUpdateResp", f->nUpdateResp);
            v->write("bSync", f->bSync);
            v->write("fVelocity", f->fVelocity);
            v->write("fPitch", f->fPitch);
            v->write("bStretchOn", f->bStretchOn);
            v->write("fStretch", f->fStretch);
            v->write("fStretchStart", f->fStretchStart);
            v->write("fStretchEnd", f->fStretchEnd);
            v->write("fStretchChunk", f->fStretchChunk);
            v->write("fStretchFade", f->fStretchFade);
            v->write("nStretchFadeType", f->nStretchFadeType);
            v->write("enLoopMode", int(f->enLoopMode));
            v->write("fLoopStart", f->fLoopStart);
            v->write("fLoopEnd", f->fLoopEnd);
            v->write("fLoopFade", f->fLoopFade);
            v->write("nLoopFadeType", f->nLoopFadeType);
            v->write("fHeadCut", f->fHeadCut);
            v->write("fTailCut", f->fTailCut);
            v->write("fFadeIn", f->fFadeIn);
            v->write("fFadeOut", f->fFadeOut);
            v->write("bPreReverse", f->bPreReverse);
            v->write("bPostReverse", f->bPostReverse);
            v->write("bCompensate", f->bCompensate);
            v->write("fCompensateFade", f->fCompensateFade);
            v->write("fCompensateChunk", f->fCompensateChunk);
            v->write("nCompensateFadeType", f->nCompensateFadeType);
            v->write("fPreDelay", f->fPreDelay);
            v->write("fMakeup", f->fMakeup);
            v->writev("fGains", f->fGains, meta::sampler_metadata::TRACKS_MAX);
            v->write("fLength", f->fLength);
            v->write("fActualLength", f->fActualLength);
            v->write("nStatus", f->nStatus);
            v->write("bOn", f->bOn);

            v->write("pFile", f->pFile);
            v->write("pPitch", f->pPitch);
            v->write("pStretchOn", f->pStretchOn);
            v->write("pStretch", f->pStretch);
            v->write("pStretchStart", f->pStretchStart);
            v->write("pStretchEnd", f->pStretchEnd);
            v->write("pStretchChunk", f->pStretchChunk);
            v->write("pStretchFade", f->pStretchFade);
            v->write("pStretchFadeType", f->pStretchFadeType);
            v->write("pLoopOn", f->pLoopOn);
            v->write("pLoopMode", f->pLoopMode);
            v->write("pLoopStart", f->pLoopStart);
            v->write("pLoopEnd", f->pLoopEnd);
            v->write("pLoopFadeType", f->pLoopFadeType);
            v->write("pLoopFade", f->pLoopFade);
            v->write("pHeadCut", f->pHeadCut);
            v->write("pTailCut", f->pTailCut);
            v->write("pFadeIn", f->pFadeIn);
            v->write("pFadeOut", f->pFadeOut);
            v->write("pMakeup", f->pMakeup);
            v->write("pVelocity", f->pVelocity);
            v->write("pPreDelay", f->pPreDelay);
            v->write("pOn", f->pOn);
            v->write("pListen", f->pListen);
            v->write("pPreReverse", f->pPreReverse);
            v->write("pPostReverse", f->pPostReverse);
            v->write("pCompensate", f->pCompensate);
            v->write("pCompensateFade", f->pCompensateFade);
            v->write("pCompensateChunk", f->pCompensateChunk);
            v->write("pCompensateFadeType", f->pCompensateFadeType);
            v->writev("pGains", f->pGains, meta::sampler_metadata::TRACKS_MAX);
            v->write("pActive", f->pActive);
            v->write("pPlayPosition", f->pPlayPosition);
            v->write("pNoteOn", f->pNoteOn);
            v->write("pLength", f->pLength);
            v->write("pActualLength", f->pActualLength);
            v->write("pStatus", f->pStatus);
            v->write("pMesh", f->pMesh);
        }

        void sampler_kernel::dump(dspu::IStateDumper *v) const
        {
            v->write("pExecutor", pExecutor);
            v->write("pGCList", pExecutor);
            v->begin_array("vFiles", vFiles, nFiles);
            {
                for (size_t i=0; i<nFiles; ++i)
                {
                    v->begin_object(v, sizeof(afile_t));
                        dump_afile(v, &vFiles[i]);
                    v->end_object();
                }
            }
            v->end_array();

            v->writev("vActive", vActive, nActive);

            v->write_object_array("vChannels", vChannels, meta::sampler_metadata::TRACKS_MAX);
            v->write_object_array("vBypass", vBypass, meta::sampler_metadata::TRACKS_MAX);
            v->write_object_array("vListen", vListen, 4);
            v->write_object("sActivity", &sActivity);
            v->write_object("sListen", &sListen);
            v->write_object("sRandom", &sRandom);
            v->write_object("sGCTask", &sGCTask);

            v->write("nFiles", nFiles);
            v->write("nActive", nActive);
            v->write("nChannels", nChannels);
            v->write("vBuffer", vBuffer);
            v->write("bBypass", bBypass);
            v->write("bReorder", bReorder);
            v->write("fFadeout", fFadeout);
            v->write("fDynamics", fDynamics);
            v->write("fDrift", fDrift);
            v->write("nSampleRate", nSampleRate);

            v->write("pDynamics", pDynamics);
            v->write("pDrift", pDrift);
            v->write("pActivity", pActivity);
            v->write("pListen", pListen);
            v->write("pData", pData);
        }
    } /* namespace plugins */
} /* namespace lsp */


