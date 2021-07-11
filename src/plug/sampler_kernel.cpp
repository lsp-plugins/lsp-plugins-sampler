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
#include <lsp-plug.in/common/debug.h>
#include <lsp-plug.in/dsp-units/units.h>
#include <lsp-plug.in/dsp-units/misc/fade.h>
#include <lsp-plug.in/dsp/dsp.h>

#define TRACE_PORT(p) lsp_trace("  port id=%s", (p)->metadata()->id);

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
        sampler_kernel::sampler_kernel()
        {
            pExecutor       = NULL;
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
            channels        = lsp_max(channels, meta::sampler_metadata::TRACKS_MAX);

            // Now we may store values
            nFiles          = files;
            nChannels       = channels;
            bReorder        = true;
            nActive         = 0;
            pExecutor       = executor;

            // Now determine object sizes
            size_t afsample_size        = align_size(sizeof(afsample_t), DEFAULT_ALIGN);
            size_t afile_size           = AFI_TOTAL * afsample_size;
            size_t array_size           = align_size(sizeof(afile_t *) * files, DEFAULT_ALIGN);

            lsp_trace("afsample_size        = %d", int(afsample_size));
            lsp_trace("afile_size           = %d", int(afile_size));
            lsp_trace("array_size           = %d", int(array_size));

            // Allocate raw chunk and link data
            size_t allocate             = array_size * 2 + afile_size * files;
            uint8_t *ptr                = alloc_aligned<uint8_t>(pData, allocate);
            if (ptr == NULL)
                return false;

            #ifdef LSP_TRACE
                uint8_t *tail               = &ptr[allocate];
                lsp_trace("allocate = %d, ptr range=%p-%p", int(allocate), ptr, tail);
            #endif /* LSP_TRACE */

            // Allocate files
            vFiles                      = new afile_t[files];
            if (vFiles == NULL)
                return false;

            vActive                     = reinterpret_cast<afile_t **>(ptr);
            ptr                        += array_size;
            lsp_trace("vActive              = %p", vActive);

            for (size_t i=0; i<files; ++i)
            {
                afile_t *af                 = &vFiles[i];

                af->nID                     = i;
                af->pLoader                 = NULL;

                af->bDirty                  = false;
                af->bSync                   = false;
                af->fVelocity               = 1.0f;
                af->fHeadCut                = 0.0f;
                af->fTailCut                = 0.0f;
                af->fFadeIn                 = 0.0f;
                af->fFadeOut                = 0.0f;
                af->bReverse                = false;
                af->fPreDelay               = meta::sampler_metadata::PREDELAY_DFL;
                af->sListen.init();
                af->bOn                     = true;
                af->fMakeup                 = 1.0f;
                af->fLength                 = 0.0f;
                af->nStatus                 = STATUS_UNSPECIFIED;

                af->pFile                   = NULL;
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
                af->pLength                 = NULL;
                af->pStatus                 = NULL;
                af->pMesh                   = NULL;
                af->pActive                 = NULL;
                af->pNoteOn                 = NULL;

                for (size_t j=0; j < meta::sampler_metadata::TRACKS_MAX; ++j)
                {
                    af->fGains[j]               = 1.0f;
                    af->pGains[j]               = NULL;
                }

                for (size_t j=0; j<AFI_TOTAL; ++j)
                {
                    afsample_t *afs             = reinterpret_cast<afsample_t *>(ptr);
                    ptr                        += afsample_size;

                    af->vData[j]                = afs;
                    lsp_trace("vFiles[%d]->vData[%d]    = %p", int(i), int(j), afs);

                    afs->pSource                = NULL;
                    afs->pSample                = NULL;
                    afs->fNorm                  = 1.0f;

                    for (size_t k=0; k<meta::sampler_metadata::TRACKS_MAX; ++k)
                        afs->vThumbs[k]     = NULL;
                }

                vActive[i]                  = NULL;
            }

            // Create additional objects: tasks for file loading
            lsp_trace("Create loaders");
            for (size_t i=0; i<files; ++i)
            {
                afile_t  *af        = &vFiles[i];

                // Create loader
                AFLoader *ldr       = new AFLoader(this, af);
                if (ldr == NULL)
                {
                    destroy_state();
                    return false;
                }

                // Store loader
                af->pLoader         = ldr;
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

            // Allocate buffer
            lsp_trace("Allocate buffer size=%d", int(meta::sampler_metadata::BUFFER_SIZE));
            float *buf              = new float[meta::sampler_metadata::BUFFER_SIZE];
            if (buf == NULL)
            {
                destroy_state();
                return false;
            }
            lsp_trace("buffer = %p", buf);
            vBuffer                 = buf;

            // Initialize toggle
            sListen.init();

            return true;
        }

        size_t sampler_kernel::bind(lltl::parray<plug::IPort> &ports, size_t port_id, bool dynamics)
        {
            lsp_trace("Binding listen toggle...");
            TRACE_PORT(ports[port_id]);
            pListen             = ports[port_id++];

            if (dynamics)
            {
                lsp_trace("Binding dynamics and drifting...");
                TRACE_PORT(ports[port_id]);
                pDynamics           = ports[port_id++];
                TRACE_PORT(ports[port_id]);
                pDrift              = ports[port_id++];
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
                TRACE_PORT(ports[port_id]);
                af->pFile               = ports[port_id++];
                TRACE_PORT(ports[port_id]);
                af->pHeadCut            = ports[port_id++];
                TRACE_PORT(ports[port_id]);
                af->pTailCut            = ports[port_id++];
                TRACE_PORT(ports[port_id]);
                af->pFadeIn             = ports[port_id++];
                TRACE_PORT(ports[port_id]);
                af->pFadeOut            = ports[port_id++];
                TRACE_PORT(ports[port_id]);
                af->pMakeup             = ports[port_id++];
                TRACE_PORT(ports[port_id]);
                af->pVelocity           = ports[port_id++];
                TRACE_PORT(ports[port_id]);
                af->pPreDelay           = ports[port_id++];
                TRACE_PORT(ports[port_id]);
                af->pOn                 = ports[port_id++];
                TRACE_PORT(ports[port_id]);
                af->pListen             = ports[port_id++];
                TRACE_PORT(ports[port_id]);
                af->pReverse            = ports[port_id++];

                for (size_t j=0; j<nChannels; ++j)
                {
                    TRACE_PORT(ports[port_id]);
                    af->pGains[j]           = ports[port_id++];
                }

                TRACE_PORT(ports[port_id]);
                af->pActive             = ports[port_id++];
                TRACE_PORT(ports[port_id]);
                af->pNoteOn             = ports[port_id++];
                TRACE_PORT(ports[port_id]);
                af->pLength             = ports[port_id++];
                TRACE_PORT(ports[port_id]);
                af->pStatus             = ports[port_id++];
                TRACE_PORT(ports[port_id]);
                af->pMesh               = ports[port_id++];
            }

            // Initialize randomizer
            sRandom.init();

            lsp_trace("Init OK");

            return port_id;
        }

        void sampler_kernel::bind_activity(plug::IPort *activity)
        {
            lsp_trace("Binding activity...");
            TRACE_PORT(activity);
            pActivity       = activity;
        }

        void sampler_kernel::destroy_state()
        {
            if (vBuffer != NULL)
            {
                delete[] vBuffer;
                vBuffer     = NULL;
            }

            for (size_t i=0; i<nChannels; ++i)
                vChannels[i].destroy(false);

            if (vFiles != NULL)
            {
                for (size_t i=0; i<nFiles;++i)
                {
                    // Delete audio file loaders
                    AFLoader *ldr   = vFiles[i].pLoader;
                    if (ldr != NULL)
                    {
                        delete ldr;
                        vFiles[i].pLoader = NULL;
                    }

                    // Destroy samples
                    for (size_t j=0; j<AFI_TOTAL; ++j)
                        destroy_afsample(vFiles[i].vData[j]);
                }

                // Drop list of files
                delete [] vFiles;
                vFiles = NULL;
            }

            free_aligned(pData);

            // Foget variables
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

        void sampler_kernel::update_settings()
        {
            // Process listen toggle
            if (pListen != NULL)
                sListen.submit(pListen->value());

            // Process file load requests
            for (size_t i=0; i<nFiles; ++i)
            {
                // Get descriptor
                afile_t *af             = &vFiles[i];
                if (af->pFile == NULL)
                    continue;

                // Get path
                plug::path_t *path = af->pFile->buffer<plug::path_t>();
                if ((path == NULL) || (!path->pending()))
                    continue;

                // Check task state
                if (af->pLoader->idle())
                {
                    // Try to submit task
                    if (pExecutor->submit(af->pLoader))
                    {
                        af->nStatus     = STATUS_LOADING;
                        lsp_trace("successfully submitted task");
                        path->accept();
                    }
                }
            }

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

                // Update sample timings
                value           = af->pHeadCut->value();
                if (value != af->fHeadCut)
                {
                    af->fHeadCut    = value;
                    af->bDirty      = true;
                }

                value           = af->pTailCut->value();
                if (value != af->fTailCut)
                {
                    af->fTailCut    = value;
                    af->bDirty      = true;
                }

                value           = af->pFadeIn->value();
                if (value != af->fFadeIn)
                {
                    af->fFadeIn     = value;
                    af->bDirty      = true;
                }

                value           = af->pFadeOut->value();
                if (value != af->fFadeOut)
                {
                    af->fFadeOut    = value;
                    af->bDirty      = true;
                }

                bool reverse    = af->pReverse->value() >= 0.5f;
                if (reverse != af->bReverse)
                {
                    af->bReverse    = reverse;
                    af->bDirty      = true;
                }
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

        void sampler_kernel::destroy_afsample(afsample_t *af)
        {
            if (af->pSource != NULL)
            {
                af->pSource->destroy();
                delete af->pSource;
                af->pSource     = NULL;
            }

            if (af->vThumbs[0] != NULL)
            {
                delete [] af->vThumbs[0];

                for (size_t i=0; i<meta::sampler_metadata::TRACKS_MAX; ++i)
                    af->vThumbs[i]      = NULL;
            }

            if (af->pSample != NULL)
            {
                af->pSample->destroy();
                delete af->pSample;
                af->pSample     = NULL;
            }
        }

        int sampler_kernel::load_file(afile_t *file)
        {
            // Load sample
            lsp_trace("file = %p", file);

            // Validate arguments
            if (file == NULL)
                return STATUS_UNKNOWN_ERR;

            // Destroy OLD data if exists
            destroy_afsample(file->vData[AFI_OLD]);

            // Check state
            afsample_t *snew        = file->vData[AFI_NEW];
            if ((snew->pSource != NULL) || (snew->pSample != NULL))
                return STATUS_UNKNOWN_ERR;

            // Check port binding
            if (file->pFile == NULL)
                return STATUS_UNKNOWN_ERR;

            // Get path
            plug::path_t *path      = file->pFile->buffer<plug::path_t>();
            if (path == NULL)
                return STATUS_UNKNOWN_ERR;

            // Get file name
            const char *fname   = path->get_path();
            if (strlen(fname) <= 0)
                return STATUS_UNSPECIFIED;

            // Load audio file
            snew->pSource           = new dspu::Sample();
            if (snew->pSource == NULL)
                return STATUS_NO_MEM;

            status_t status = snew->pSource->load(fname, meta::sampler_metadata::SAMPLE_LENGTH_MAX * 0.001f);
            if (status != STATUS_OK)
            {
                lsp_trace("load failed: status=%d (%s)", status, get_status(status));
                destroy_afsample(snew);
                return status;
            }

            status = snew->pSource->resample(nSampleRate);
            if (status != STATUS_OK)
            {
                lsp_trace("resample failed: status=%d (%s)", status, get_status(status));
                destroy_afsample(snew);
                return status;
            }

            // Create samples
            size_t channels     = snew->pSource->channels();
            size_t samples      = snew->pSource->samples();
            if (channels > nChannels)
                channels           = nChannels;

            float *thumbs   = new float[channels * meta::sampler_metadata::MESH_SIZE];
            if (thumbs == NULL)
            {
                destroy_afsample(snew);
                return STATUS_NO_MEM;
            }

            snew->vThumbs[0]        = thumbs;
            float max = 0.0f;

            // Create and initialize sample
            snew->pSample           = new dspu::Sample();
            if ((snew->pSample == NULL) || (!snew->pSample->init(channels, samples)))
            {
                lsp_trace("sample initialization failed");
                destroy_afsample(snew);
                return STATUS_NO_MEM;
            }

            // Determine the normalizing factor
            for (size_t i=0; i<channels; ++i)
            {
                snew->vThumbs[i]        = thumbs;
                thumbs                 += meta::sampler_metadata::MESH_SIZE;

                // Determine the maximum amplitude
                float a_max             = dsp::abs_max(snew->pSource->channel(i), samples);
                lsp_trace("dsp::abs_max(%p, %d): a_max=%f", snew->pFile->channel(i), int(samples), a_max);
                max                     = lsp_max(max, a_max);
            }

            lsp_trace("max=%f", max);
            snew->fNorm     = (max != 0.0f) ? 1.0f / max : 1.0f;

            lsp_trace("file successful loaded: %s", fname);

            return STATUS_OK;
        }

        void sampler_kernel::copy_asample(afsample_t *dst, const afsample_t *src)
        {
            dst->pSource        = src->pSource;
            dst->pSample        = src->pSample;
            dst->fNorm          = src->fNorm;

            for (size_t j=0; j<meta::sampler_metadata::TRACKS_MAX; ++j)
                dst->vThumbs[j]     = src->vThumbs[j];
        }

        void sampler_kernel::clear_asample(afsample_t *dst)
        {
            dst->pSource        = NULL;
            dst->pSample        = NULL;
            dst->fNorm          = 1.0f;

            for (size_t j=0; j<meta::sampler_metadata::TRACKS_MAX; ++j)
                dst->vThumbs[j]     = NULL;
        }

        void sampler_kernel::render_sample(afile_t *af)
        {
            // Get maximum sample count
            afsample_t *afs     = af->vData[AFI_CURR];
            if (afs->pSource != NULL)
            {
                ssize_t head        = dspu::millis_to_samples(nSampleRate, af->fHeadCut);
                ssize_t tail        = dspu::millis_to_samples(nSampleRate, af->fTailCut);
                ssize_t tot_samples = dspu::millis_to_samples(nSampleRate, af->fLength);
                ssize_t max_samples = tot_samples - head - tail;
                dspu::Sample *s     = afs->pSample;

                if (max_samples > 0)
                {
                    lsp_trace("re-render sample max_samples=%d", int(max_samples));

                    // Re-render sample
                    for (size_t j=0; j<s->channels(); ++j)
                    {
                        float *dst          = s->getBuffer(j);
                        const float *src    = afs->pSource->channel(j);

                        if (af->bReverse)
                            dsp::reverse2(dst, &src[tail], max_samples);
                        else
                            dsp::copy(dst, &src[head], max_samples);

                        // Apply fade-in and fade-out to the buffer
                        dspu::fade_in(dst, dst, dspu::millis_to_samples(nSampleRate, af->fFadeIn), max_samples);
                        dspu::fade_out(dst, dst, dspu::millis_to_samples(nSampleRate, af->fFadeOut), max_samples);

                        // Now render thumbnail
                        src                 = dst;
                        dst                 = afs->vThumbs[j];
                        for (size_t k=0; k<meta::sampler_metadata::MESH_SIZE; ++k)
                        {
                            size_t first    = (k * max_samples) / meta::sampler_metadata::MESH_SIZE;
                            size_t last     = ((k + 1) * max_samples) / meta::sampler_metadata::MESH_SIZE;
                            if (first < last)
                                dst[k]          = dsp::abs_max(&src[first], last - first);
                            else
                                dst[k]          = fabs(src[first]);
                        }

                        // Normalize graph if possible
                        if (afs->fNorm != 1.0f)
                            dsp::mul_k2(dst, afs->fNorm, meta::sampler_metadata::MESH_SIZE);
                    }

                    // Update length of the sample
                    s->set_length(max_samples);

                    // (Re)bind sample
                    for (size_t j=0; j<nChannels; ++j)
                        vChannels[j].bind(af->nID, s, false);
                }
                else
                {
                    // Cleanup sample data
                    for (size_t j=0; j<s->channels(); ++j)
                        dsp::fill_zero(afs->vThumbs[j], meta::sampler_metadata::MESH_SIZE);

                    // Unbind empty sample
                    for (size_t j=0; j<nChannels; ++j)
                        vChannels[j].unbind(af->nID);
                }
            }
            else
            {
                // Unbind empty sample
                for (size_t j=0; j<nChannels; ++j)
                    vChannels[j].unbind(af->nID);
            }

            // Reset dirty flag and set sync flag
            af->bDirty      = false;
            af->bSync       = true;
        }

        void sampler_kernel::reorder_samples()
        {
            lsp_trace("Reordering active files");

            // Compute the list of active files
            nActive     = 0;
            for (size_t i=0; i<nFiles; ++i)
            {
                if (!vFiles[i].bOn)
                    continue;
                if (vFiles[i].vData[AFI_CURR]->pSample == NULL)
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
                        {
                            // Swap file pointers
                            afile_t    *af  = vActive[i];
                            vActive[i]      = vActive[j];
                            vActive[j]      = af;
                        }
            }

            #ifdef LSP_TRACE
                for (size_t i=0; i<nActive; ++i)
                    lsp_trace("active file #%d: velocity=%.3f", int(vActive[i]->nID), vActive[i]->fVelocity);
            #endif /* LSP_TRACE */
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

        void sampler_kernel::process_file_load_requests()
        {
            for (size_t i=0; i<nFiles; ++i)
            {
                // Get descriptor
                afile_t *af         = &vFiles[i];
                if (af->pFile == NULL)
                    continue;

                // Get path and check task state
                plug::path_t *path = af->pFile->buffer<plug::path_t>();
                if ((path != NULL) && (path->accepted()) && (af->pLoader->completed()))
                {
                    // Task has been completed
                    lsp_trace("task has been completed");

                    // Update state of audio file
                    copy_asample(af->vData[AFI_OLD], af->vData[AFI_CURR]);
                    copy_asample(af->vData[AFI_CURR], af->vData[AFI_NEW]);
                    clear_asample(af->vData[AFI_NEW]);

                    afsample_t *afs = af->vData[AFI_CURR];
                    af->nStatus     = af->pLoader->code();
                    af->bDirty      = true; // Mark sample for re-rendering
                    af->fLength     = (af->nStatus == STATUS_OK) ? dspu::samples_to_millis(nSampleRate, afs->pSource->samples()) : 0.0f;

                    lsp_trace("Current file: status=%d (%s), length=%f msec\n",
                        int(af->nStatus), get_status(af->nStatus), af->fLength);

                    // Now we surely can commit changes and reset task state
                    path->commit();
                    af->pLoader->reset();

                    // Trigger the state for reorder
                    bReorder        = true;
                }

                // Check that we need to re-render sample
                if (af->bDirty)
                    render_sample(af);
            }
        }

        void sampler_kernel::process(float **outs, const float **ins, size_t samples)
        {
            // Step 1
            // Process file load requests
            process_file_load_requests();

            // Reorder the files in ascending velocity order if needed
            if (bReorder)
            {
                // Reorder samples and reset the reorder flag
                reorder_samples();
                bReorder = false;
            }

            // Step 2
            // Process events
            process_listen_events();

            // Step 3
            // Process the channels individually
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

            // Step 4
            // Output parameters
            output_parameters(samples);
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
                af->pStatus->set_value(af->nStatus);

                // Output information about the activity
                af->pNoteOn->set_value(af->sNoteOn.process(samples));

                // Get file sample
                afsample_t *afs     = af->vData[AFI_CURR];
                size_t channels     = (afs->pSample != NULL) ? afs->pSample->channels() : 0;
                if (channels > nChannels)
                    channels             =  nChannels;

                // Output activity flag
                af->pActive->set_value(((af->bOn) && (channels > 0)) ? 1.0f : 0.0f);

                // Store file thumbnails to mesh
                plug::mesh_t *mesh  = reinterpret_cast<plug::mesh_t *>(af->pMesh->buffer());
                if ((mesh == NULL) || (!mesh->isEmpty()) || (!af->bSync))
                    continue;

                if (channels > 0)
                {
                    // Copy thumbnails
                    for (size_t j=0; j<channels; ++j)
                        dsp::copy(mesh->pvData[j], afs->vThumbs[j], meta::sampler_metadata::MESH_SIZE);

                    mesh->data(channels, meta::sampler_metadata::MESH_SIZE);
                }
                else
                    mesh->data(0, 0);

                af->bSync           = false;
            }
        }

        void sampler_kernel::dump_afsample(dspu::IStateDumper *v, const afsample_t *f) const
        {
            if (f != NULL)
            {
                v->begin_object(f, sizeof(afsample_t));
                {
                    v->write_object("pSource", f->pSource);
                    v->write_object("pSample", f->pSample);
                    v->write("vThumbs", f->fNorm);
                    v->write("vThumbs", f->fNorm);
                }
                v->end_object();
            }
            else
                v->write(f);
        }

        void sampler_kernel::dump_afile(dspu::IStateDumper *v, const afile_t *f) const
        {
            v->write("nID", f->nID);
            v->write_object("pLoader", f->pLoader);
            v->write_object("sListen", &f->sListen);
            v->write_object("sNoteOn", &f->sNoteOn);

            v->write("bDirty", f->bDirty);
            v->write("bSync", f->bSync);
            v->write("fVelocity", f->fVelocity);
            v->write("fHeadCut", f->fHeadCut);
            v->write("fTailCut", f->fTailCut);
            v->write("fFadeIn", f->fFadeIn);
            v->write("fFadeOut", f->fFadeOut);
            v->write("bReverse", f->bReverse);
            v->write("fPreDelay", f->fPreDelay);
            v->write("fMakeup", f->fMakeup);
            v->writev("fGains", f->fGains, meta::sampler_metadata::TRACKS_MAX);
            v->write("fLength", f->fLength);
            v->write("nStatus", f->nStatus);
            v->write("bOn", f->bOn);

            v->write("pFile", f->pFile);
            v->write("pHeadCut", f->pHeadCut);
            v->write("pTailCut", f->pTailCut);
            v->write("pFadeIn", f->pFadeIn);
            v->write("pFadeOut", f->pFadeOut);
            v->write("pMakeup", f->pMakeup);
            v->write("pVelocity", f->pVelocity);
            v->write("pPreDelay", f->pPreDelay);
            v->write("pListen", f->pListen);
            v->write("pReverse", f->pReverse);
            v->writev("pGains", f->pGains, meta::sampler_metadata::TRACKS_MAX);
            v->write("pLength", f->pLength);
            v->write("pStatus", f->pStatus);
            v->write("pMesh", f->pMesh);
            v->write("pNoteOn", f->pNoteOn);
            v->write("pOn", f->pOn);
            v->write("pActive", f->pActive);

            v->begin_array("vData", f->vData, AFI_TOTAL);
            {
                for (size_t i=0; i<AFI_TOTAL; ++i)
                    dump_afsample(v, f->vData[i]);
            }
        }

        void sampler_kernel::dump(dspu::IStateDumper *v) const
        {
            v->write("pExecutor", pExecutor);
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


