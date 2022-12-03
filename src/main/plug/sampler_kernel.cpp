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

#include <private/plugins/sampler_kernel.h>
#include <lsp-plug.in/common/alloc.h>
#include <lsp-plug.in/common/atomic.h>
#include <lsp-plug.in/common/debug.h>
#include <lsp-plug.in/dsp-units/units.h>
#include <lsp-plug.in/dsp-units/misc/fade.h>
#include <lsp-plug.in/dsp/dsp.h>

namespace lsp
{
    static plug::IPort *TRACE_PORT(plug::IPort *p)
    {
        lsp_trace("  port id=%s", (p)->metadata()->id);
        return p;
    }

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
            vFiles                      = reinterpret_cast<afile_t *>(ptr);
            ptr                        += afile_szof;
            vActive                     = reinterpret_cast<afile_t **>(ptr);
            ptr                        += vactive_szof;
            vBuffer                     = reinterpret_cast<float *>(ptr);
            ptr                        += vbuffer_szof;

            for (size_t i=0; i<files; ++i)
            {
                afile_t *af                 = &vFiles[i];

                af->nID                     = i;
                af->pLoader                 = NULL;
                af->pRenderer               = NULL;

                af->sListen.construct();
                af->sNoteOn.construct();
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
                af->fHeadCut                = 0.0f;
                af->fTailCut                = 0.0f;
                af->fFadeIn                 = 0.0f;
                af->fFadeOut                = 0.0f;
                af->bReverse                = false;
                af->bCompensate             = false;
                af->fCompensateFade         = 0.0f;
                af->fCompensateChunk        = 0.0f;
                af->nCompensateFadeType     = XFADE_DFL;
                af->fPreDelay               = meta::sampler_metadata::PREDELAY_DFL;
                af->sListen.init();
                af->bOn                     = true;
                af->fMakeup                 = 1.0f;
                af->fLength                 = 0.0f;
                af->fActualLength           = 0.0f;
                af->nStatus                 = STATUS_UNSPECIFIED;

                af->pFile                   = NULL;
                af->pPitch                  = NULL;
                af->pStretchOn              = NULL;
                af->pStretch                = NULL;
                af->pStretchStart           = NULL;
                af->pStretchEnd             = NULL;
                af->pStretchChunk           = NULL;
                af->pStretchFade            = NULL;
                af->pStretchFadeType        = NULL;
                af->pHeadCut                = NULL;
                af->pTailCut                = NULL;
                af->pFadeIn                 = NULL;
                af->pFadeOut                = NULL;
                af->pVelocity               = NULL;
                af->pMakeup                 = NULL;
                af->pPreDelay               = NULL;
                af->pOn                     = NULL;
                af->pListen                 = NULL;
                af->pReverse                = NULL;
                af->pCompensate             = NULL;
                af->pCompensateFade         = NULL;
                af->pCompensateChunk        = NULL;
                af->pCompensateFadeType     = NULL;
                af->pLength                 = NULL;
                af->pActualLength           = NULL;
                af->pStatus                 = NULL;
                af->pMesh                   = NULL;
                af->pActive                 = NULL;
                af->pNoteOn                 = NULL;

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

        size_t sampler_kernel::bind(plug::IPort **ports, size_t port_id, bool dynamics)
        {
            lsp_trace("Binding listen toggle...");
            pListen             = TRACE_PORT(ports[port_id++]);

            if (dynamics)
            {
                lsp_trace("Binding dynamics and drifting...");
                pDynamics           = TRACE_PORT(ports[port_id++]);
                pDrift              = TRACE_PORT(ports[port_id++]);
            }

            lsp_trace("Skipping sample selector port...");
            TRACE_PORT(ports[port_id]);
            port_id++;

            // Iterate each file
            for (size_t i=0; i<nFiles; ++i)
            {
                lsp_trace("Binding sample %d", int(i));

                afile_t *af             = &vFiles[i];
                // Allocate files
                af->pFile               = TRACE_PORT(ports[port_id++]);
                af->pPitch              = TRACE_PORT(ports[port_id++]);
                af->pStretchOn          = TRACE_PORT(ports[port_id++]);
                af->pStretch            = TRACE_PORT(ports[port_id++]);
                af->pStretchStart       = TRACE_PORT(ports[port_id++]);
                af->pStretchEnd         = TRACE_PORT(ports[port_id++]);
                af->pStretchChunk       = TRACE_PORT(ports[port_id++]);
                af->pStretchFade        = TRACE_PORT(ports[port_id++]);
                af->pStretchFadeType    = TRACE_PORT(ports[port_id++]);
                af->pHeadCut            = TRACE_PORT(ports[port_id++]);
                af->pTailCut            = TRACE_PORT(ports[port_id++]);
                af->pFadeIn             = TRACE_PORT(ports[port_id++]);
                af->pFadeOut            = TRACE_PORT(ports[port_id++]);
                af->pMakeup             = TRACE_PORT(ports[port_id++]);
                af->pVelocity           = TRACE_PORT(ports[port_id++]);
                af->pPreDelay           = TRACE_PORT(ports[port_id++]);
                af->pOn                 = TRACE_PORT(ports[port_id++]);
                af->pListen             = TRACE_PORT(ports[port_id++]);
                af->pReverse            = TRACE_PORT(ports[port_id++]);
                af->pCompensate         = TRACE_PORT(ports[port_id++]);
                af->pCompensateFade     = TRACE_PORT(ports[port_id++]);
                af->pCompensateChunk    = TRACE_PORT(ports[port_id++]);
                af->pCompensateFadeType = TRACE_PORT(ports[port_id++]);

                for (size_t j=0; j<nChannels; ++j)
                    af->pGains[j]           = TRACE_PORT(ports[port_id++]);

                af->pActive             = TRACE_PORT(ports[port_id++]);
                af->pNoteOn             = TRACE_PORT(ports[port_id++]);
                af->pLength             = TRACE_PORT(ports[port_id++]);
                af->pActualLength       = TRACE_PORT(ports[port_id++]);
                af->pStatus             = TRACE_PORT(ports[port_id++]);
                af->pMesh               = TRACE_PORT(ports[port_id++]);
            }

            // Initialize randomizer
            sRandom.init();

            lsp_trace("Init OK");

            return port_id;
        }

        void sampler_kernel::bind_activity(plug::IPort *activity)
        {
            lsp_trace("Binding activity...");
            pActivity       = TRACE_PORT(activity);
        }

        void sampler_kernel::destroy_sample(dspu::Sample * &sample)
        {
            if (sample == NULL)
                return;

            sample->destroy();
            delete sample;
            lsp_trace("Destroyed sample %p", sample);
            sample  = NULL;
        }

        void sampler_kernel::destroy_afile(afile_t *af)
        {
            af->sListen.destroy();
            af->sNoteOn.destroy();

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
                gc_list->destroy();
                delete gc_list;
                lsp_trace("Destroyed sample %p", gc_list);
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
                sp->stop();
                sp->unbind_all();
                dspu::Sample *gc_list = sp->gc();
                destroy_samples(gc_list);
                sp->destroy(false);
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
        void sampler_kernel::commit_afile_value(afile_t *af, T & field, plug::IPort *port)
        {
            const T temp = port->value();
            if (temp != field)
            {
                field       = temp;
                ++af->nUpdateReq;
            }
        }

        void sampler_kernel::commit_afile_value(afile_t *af, bool & field, plug::IPort *port)
        {
            const bool temp = port->value() >= 0.5f;
            if (temp != field)
            {
                field       = temp;
                ++af->nUpdateReq;
            }
        }

        void sampler_kernel::update_settings()
        {
            // Process listen toggle
            if (pListen != NULL)
                sListen.submit(pListen->value());

            // Update note and octave
            lsp_trace("Initializing samples...");

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
                commit_afile_value(af, af->fPitch, af->pPitch);
                commit_afile_value(af, af->bStretchOn, af->pStretchOn);
                commit_afile_value(af, af->fStretch, af->pStretch);
                commit_afile_value(af, af->fStretchStart, af->pStretchStart);
                commit_afile_value(af, af->fStretchEnd, af->pStretchEnd);
                commit_afile_value(af, af->fStretchChunk, af->pStretchChunk);
                commit_afile_value(af, af->fStretchFade, af->pStretchFade);
                commit_afile_value(af, af->nStretchFadeType, af->pStretchFadeType);
                commit_afile_value(af, af->fHeadCut, af->pHeadCut);
                commit_afile_value(af, af->fTailCut, af->pTailCut);
                commit_afile_value(af, af->fFadeIn, af->pFadeIn);
                commit_afile_value(af, af->fFadeOut, af->pFadeOut);
                commit_afile_value(af, af->bReverse, af->pReverse);
                commit_afile_value(af, af->bCompensate, af->pCompensate);
                commit_afile_value(af, af->fCompensateFade, af->pCompensateFade);
                commit_afile_value(af, af->fCompensateChunk, af->pCompensateChunk);
                commit_afile_value(af, af->nCompensateFadeType, af->pCompensateFadeType);
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
            size_t channels         = lsp_min(nChannels, source->channels());
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
            ssize_t src_samples     = src->length();
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
            if (af->bCompensate)
            {
                size_t chunk_size       = dspu::millis_to_samples(nSampleRate, af->fCompensateChunk);
                dspu::sample_crossfade_t fade_type  = (af->nCompensateFadeType == XFADE_LINEAR) ?
                    dspu::SAMPLE_CROSSFADE_LINEAR :
                    dspu::SAMPLE_CROSSFADE_CONST_POWER;
                float crossfade         = lsp_limit(af->fCompensateFade * 0.01f, 0.0f, 1.0f);

                if ((res = temp.stretch(src_samples, chunk_size, fade_type, crossfade)) != STATUS_OK)
                    return res;
            }

            // Determine the normalizing factor
            ssize_t samples         = temp.length();
            float abs_max           = 0.0f;
            for (size_t i=0; i<channels; ++i)
            {
                // Determine the maximum amplitude
                float a_max             = dsp::abs_max(temp.channel(i), samples);
                abs_max                 = lsp_max(abs_max, a_max);
            }
            float norming       = (abs_max != 0.0f) ? 1.0f / abs_max : 1.0f;

            // Render the thumbnails
            for (size_t j=0; j<channels; ++j)
            {
                const float *src    = temp.channel(j);
                float *dst          = af->vThumbs[j];
                for (size_t k=0; k<meta::sampler_metadata::MESH_SIZE; ++k)
                {
                    size_t first    = (k * samples) / meta::sampler_metadata::MESH_SIZE;
                    size_t last     = ((k + 1) * samples) / meta::sampler_metadata::MESH_SIZE;
                    if (first < last)
                        dst[k]          = dsp::abs_max(&src[first], last - first);
                    else
                        dst[k]          = fabs(src[first]);
                }

                // Normalize graph if possible
                if (norming != 1.0f)
                    dsp::mul_k2(dst, norming, meta::sampler_metadata::MESH_SIZE);
            }
            af->fLength             = dspu::samples_to_millis(nSampleRate, samples);

            // Perform stretch of the sample
            ssize_t stretch_delta   = (af->bStretchOn) ? dspu::millis_to_samples(nSampleRate, af->fStretch) : 0.0f;

            if (stretch_delta != 0)
            {
                ssize_t s_begin         = lsp_limit(dspu::millis_to_samples(nSampleRate, af->fStretchStart), 0, temp.length());
                ssize_t s_end           = lsp_limit(dspu::millis_to_samples(nSampleRate, af->fStretchEnd), 0, temp.length());
                if (s_end < s_begin)
                    lsp::swap(s_begin, s_end);
                ssize_t s_length        = lsp_max(s_end + stretch_delta - s_begin, 0);
                size_t chunk_size       = dspu::millis_to_samples(nSampleRate, af->fStretchChunk);
                dspu::sample_crossfade_t fade_type  = (af->nStretchFadeType == XFADE_LINEAR) ?
                    dspu::SAMPLE_CROSSFADE_LINEAR :
                    dspu::SAMPLE_CROSSFADE_CONST_POWER;
                float crossfade         = lsp_limit(af->fStretchFade * 0.01f, 0.0f, 1.0f);

                // Perform stretch only when it is possible, do not report errors if stretch didn't succeed
                temp.stretch(s_length, chunk_size, fade_type, crossfade, s_begin, s_end);
                samples                 = temp.length();
            }

            // Perform the head and tail cut operations
            ssize_t head_cut    = dspu::millis_to_samples(nSampleRate, af->fHeadCut);
            ssize_t tail_cut    = dspu::millis_to_samples(nSampleRate, af->fTailCut);
            ssize_t c_begin     = lsp_limit(head_cut, 0, samples);
            ssize_t c_end       = lsp_limit(samples - tail_cut, c_begin, samples);
            samples             = c_end - c_begin;

            // Initialize target sample
            dspu::Sample *out   = new dspu::Sample();
            if (out == NULL)
                return STATUS_NO_MEM;
            lsp_trace("Allocated sample %p", out);
            lsp_finally { destroy_sample(out); };

            if (!out->resize(channels, samples, samples))
            {
                lsp_warn("Error initializing playback sample");
                return STATUS_NO_MEM;
            }

            // Apply fade-in and fade-out to the buffer
            ssize_t fade_in     = dspu::millis_to_samples(nSampleRate, af->fFadeIn);
            ssize_t fade_out    = dspu::millis_to_samples(nSampleRate, af->fFadeOut);
            for (size_t j=0; j<channels; ++j)
            {
                const float *src    = temp.channel(j);
                float *dst          = out->getBuffer(j);

                dspu::fade_in(dst, &src[c_begin], fade_in, samples);
                dspu::fade_out(dst, dst, fade_out, samples);
                if (af->bReverse)
                    dsp::reverse1(dst, samples);
            }
            af->fActualLength       = dspu::samples_to_millis(nSampleRate, samples);

            // Commit the new sample to the processed
            lsp::swap(out, af->pProcessed);

            return STATUS_OK;
        }

        void sampler_kernel::play_sample(const afile_t *af, float gain, size_t delay)
        {
            lsp_trace("id=%d, gain=%f, delay=%d", int(af->nID), gain, int(delay));

            // Scale the final output gain
            gain    *= af->fMakeup;

            if (nChannels == 1)
            {
                lsp_trace("channels[%d].play(%d, %d, %f, %d)", int(0), int(af->nID), int(0), gain * af->fGains[0], int(delay));
                vChannels[0].play(af->nID, 0, gain * af->fGains[0], delay);
            }
            else if (nChannels == 2)
            {
                for (size_t i=0; i<nChannels; ++i)
                {
                    size_t j=i^1; // j = (i + 1) % 2
                    lsp_trace("channels[%d].play(%d, %d, %f, %d)", int(i), int(af->nID), int(i), gain * af->fGains[i], int(delay));
                    vChannels[i].play(af->nID, i, gain * af->fGains[i], delay);
                    lsp_trace("channels[%d].play(%d, %d, %f, %d)", int(j), int(i), int(af->nID), gain * (1.0f - af->fGains[i]), int(delay));
                    vChannels[j].play(af->nID, i, gain * (1.0f - af->fGains[i]), delay);
                }
            }
            else
            {
                for (size_t i=0; i<nChannels; ++i)
                {
                    lsp_trace("channels[%d].play(%d, %d, %f, %d)", int(i), int(af->nID), int(i), gain * af->fGains[i], int(delay));
                    vChannels[i].play(af->nID, i, gain * af->fGains[i], delay);
                }
            }
        }

        void sampler_kernel::cancel_sample(const afile_t *af, size_t fadeout, size_t delay)
        {
            lsp_trace("id=%d, delay=%d", int(af->nID), int(delay));

            // Cancel all playbacks
            for (size_t i=0; i<nChannels; ++i)
            {
                lsp_trace("channels[%d].cancel(%d, %d, %d)", int(af->nID), int(i), int(fadeout), int(delay));
                vChannels[i].cancel_all(af->nID, i, fadeout, delay);
            }
        }

        void sampler_kernel::trigger_on(size_t timestamp, float level)
        {
            if (nActive <= 0)
                return;

            // Binary search of sample
            lsp_trace("normalized velocity = %f", level);
            level      *=   100.0f; // Make velocity in percentage
            ssize_t f_first = 0, f_last = nActive-1;
            while (f_last > f_first)
            {
                ssize_t f_mid = (f_last + f_first) >> 1;
                if (level <= vActive[f_mid]->fVelocity)
                    f_last  = f_mid;
                else
                    f_first = f_mid + 1;
            }
            if (f_last < 0)
                f_last      = 0;
            else if (f_last >= ssize_t(nActive))
                f_last      = nActive - 1;

            // Get the file and ajdust gain
            afile_t *af     = vActive[f_last];
            size_t delay    = dspu::millis_to_samples(nSampleRate, af->fPreDelay) + timestamp;

            lsp_trace("f_last=%d, af->id=%d, af->velocity=%.3f", int(f_last), int(af->nID), af->fVelocity);

            // Apply changes to all ports
            if (af->fVelocity > 0.0f)
            {
                // Apply 'Humanisation' parameters
                level       = level * ((1.0f - fDynamics*0.5) + fDynamics * sRandom.random(dspu::RND_EXP)) / af->fVelocity;
                delay      += dspu::millis_to_samples(nSampleRate, fDrift) * sRandom.random(dspu::RND_EXP);

                // Play sample
                play_sample(af, level, delay);

                // Trigger the note On indicator
                af->sNoteOn.blink();
                sActivity.blink();
            }
        }

        void sampler_kernel::trigger_off(size_t timestamp, float level)
        {
            if (nActive <= 0)
                return;

            size_t delay    = timestamp;
            size_t fadeout  = dspu::millis_to_samples(nSampleRate, fFadeout);

            for (size_t i=0; i<nActive; ++i)
                cancel_sample(vActive[i], fadeout, delay);
        }

        void sampler_kernel::trigger_stop(size_t timestamp)
        {
            // Apply changes to all ports
            for (size_t j=0; j<nChannels; ++j)
                vChannels[j].stop();
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
                trigger_on(0, 0.5f);
                sListen.commit();
            }

            for (size_t i=0; i<nFiles; ++i)
            {
                // Get descriptor
                afile_t *af         = &vFiles[i];
                if (af->pFile == NULL)
                    continue;

                // Trigger the event
                if (af->sListen.pending())
                {
                    // Play sample
                    play_sample(af, 0.5f, 0); // Listen at mid-velocity

                    // Update states
                    af->sListen.commit();
                    af->sNoteOn.blink();
                }
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

                // Store file thumbnails to mesh
                plug::mesh_t *mesh  = reinterpret_cast<plug::mesh_t *>(af->pMesh->buffer());
                if ((mesh == NULL) || (!mesh->isEmpty()) || (!af->bSync) || (!af->pLoader->idle()))
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

        void sampler_kernel::dump_afile(dspu::IStateDumper *v, const afile_t *f) const
        {
            v->write("nID", f->nID);
            v->write_object("pLoader", f->pLoader);
            v->write_object("pOriginal", f->pOriginal);
            v->write_object("pProcessed", f->pProcessed);
            v->write("vThumbs", f->vThumbs);
            v->write_object("sListen", &f->sListen);
            v->write_object("sNoteOn", &f->sNoteOn);

            v->write("nUpdateReq", f->nUpdateReq);
            v->write("nUpdateResp", f->nUpdateResp);
            v->write("bSync", f->bSync);
            v->write("fVelocity", f->fVelocity);
            v->write("fPitch", f->fPitch);
            v->write("bStretch", f->bStretchOn);
            v->write("fStretch", f->fStretch);
            v->write("fStretchStart", f->fStretchStart);
            v->write("fStretchEnd", f->fStretchEnd);
            v->write("fStretchChunk", f->fStretchChunk);
            v->write("fStretchFade", f->fStretchFade);
            v->write("nStretchFadeType", f->nStretchFadeType);
            v->write("fHeadCut", f->fHeadCut);
            v->write("fTailCut", f->fTailCut);
            v->write("fFadeIn", f->fFadeIn);
            v->write("fFadeOut", f->fFadeOut);
            v->write("bReverse", f->bReverse);
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
            v->write("pHeadCut", f->pHeadCut);
            v->write("pTailCut", f->pTailCut);
            v->write("pFadeIn", f->pFadeIn);
            v->write("pFadeOut", f->pFadeOut);
            v->write("pMakeup", f->pMakeup);
            v->write("pVelocity", f->pVelocity);
            v->write("pPreDelay", f->pPreDelay);
            v->write("pListen", f->pListen);
            v->write("pReverse", f->pReverse);
            v->write("pCompensate", f->pCompensate);
            v->write("pCompensateFade", f->pCompensateFade);
            v->write("pCompensateChunk", f->pCompensateChunk);
            v->write("pCompensateFadeType", f->pCompensateFadeType);
            v->writev("pGains", f->pGains, meta::sampler_metadata::TRACKS_MAX);
            v->write("pLength", f->pLength);
            v->write("pActualLength", f->pActualLength);
            v->write("pStatus", f->pStatus);
            v->write("pMesh", f->pMesh);
            v->write("pNoteOn", f->pNoteOn);
            v->write("pOn", f->pOn);
            v->write("pActive", f->pActive);
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


