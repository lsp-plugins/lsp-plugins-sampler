/*
 * Copyright (C) 2025 Linux Studio Plugins Project <https://lsp-plug.in/>
 *           (C) 2025 Vladimir Sadovnikov <sadko4u@gmail.com>
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

#include <private/plugins/sampler.h>
#include <lsp-plug.in/common/alloc.h>
#include <lsp-plug.in/common/debug.h>
#include <lsp-plug.in/dsp/dsp.h>
#include <lsp-plug.in/shared/debug.h>

namespace lsp
{
    namespace plugins
    {
        //---------------------------------------------------------------------
        // Plugin factory
        inline namespace
        {
            typedef struct plugin_settings_t
            {
                const meta::plugin_t   *metadata;
                uint8_t                 samplers;
                uint8_t                 channels;
                bool                    dry_ports;
            } plugin_settings_t;

            static const meta::plugin_t *plugins[] =
            {
                &meta::sampler_mono,
                &meta::sampler_stereo,
                &meta::multisampler_x12,
                &meta::multisampler_x24,
                &meta::multisampler_x48,
                &meta::multisampler_x12_do,
                &meta::multisampler_x24_do,
                &meta::multisampler_x48_do
            };

            static const plugin_settings_t plugin_settings[] =
            {
                { &meta::sampler_mono,          1,  1, false    },
                { &meta::sampler_stereo,        1,  2, false    },
                { &meta::multisampler_x12,      12, 2, false    },
                { &meta::multisampler_x24,      24, 2, false    },
                { &meta::multisampler_x48,      48, 2, false    },
                { &meta::multisampler_x12_do,   12, 2, true     },
                { &meta::multisampler_x24_do,   24, 2, true     },
                { &meta::multisampler_x48_do,   48, 2, true     },
                { NULL, 0, 0, false }
            };

            static plug::Module *plugin_factory(const meta::plugin_t *meta)
            {
                for (const plugin_settings_t *s = plugin_settings; s->metadata != NULL; ++s)
                    if (s->metadata == meta)
                        return new sampler(s->metadata, s->samplers, s->channels, s->dry_ports);
                return NULL;
            }

            static plug::Factory factory(plugin_factory, plugins, 8);
        } /* inline namespace */

        //-------------------------------------------------------------------------
        sampler::sampler(const meta::plugin_t *metadata, size_t samplers, size_t channels, bool dry_ports):
            plug::Module(metadata)
        {
            nChannels       = channels;
            nSamplers       = lsp_min(meta::sampler_metadata::INSTRUMENTS_MAX, samplers);
            nFiles          = meta::sampler_metadata::SAMPLE_FILES;
            nDOMode         = 0;
            bDryPorts       = dry_ports;
            vSamplers       = NULL;

            for (size_t i=0; i<meta::sampler_metadata::TRACKS_MAX; ++i)
            {
                channel_t *tc   = &vChannels[i];

                tc->vIn         = NULL;
                tc->vOut        = NULL;
                tc->vTmpIn      = NULL;
                tc->vTmpOut     = NULL;
                tc->vTmpListen  = NULL;
                tc->pIn         = NULL;
                tc->pOut        = NULL;
            }

            pBuffer         = NULL;
            fDry            = 1.0f;
            fWet            = 1.0f;
            bMuting         = false;

            pMidiIn         = NULL;
            pMidiOut        = NULL;

            pBypass         = NULL;
            pMute           = NULL;
            pMuting         = NULL;
            pNoteOff        = NULL;
            pFadeout        = NULL;
            pDry            = NULL;
            pWet            = NULL;
            pDryWet         = NULL;
            pGain           = NULL;
            pDOGain         = NULL;
            pDOPan          = NULL;
        }

        sampler::~sampler()
        {
            do_destroy();
        }

        void sampler::init(plug::IWrapper *wrapper, plug::IPort **ports)
        {
            // Pass wrapper
            plug::Module::init(wrapper, ports);

            // Allocate samplers
            vSamplers       = new sampler_t[nSamplers];
            lsp_trace("samplers = %d, channels=%d, vSamplers=%p", int(nSamplers), int(nChannels), vSamplers);
            if (vSamplers == NULL)
                return;

            // Initialize toggle
            sMute.init();

            // Initialize samplers
            ipc::IExecutor *executor    = wrapper->executor();

            for (size_t i=0; i<nSamplers; ++i)
            {
                // Get sampler pointer
                sampler_t *s = &vSamplers[i];

                // Initialize sampler
                lsp_trace("Initializing sampler #%d...", int(i));
                if (!s->sSampler.init(executor, nFiles, nChannels))
                    return;

                s->nNote        = meta::sampler_metadata::NOTE_DFL + meta::sampler_metadata::OCTAVE_DFL * 12;
                s->nChannelMap  = select_channels(meta::sampler_metadata::CHANNEL_DFL);
                s->nMuteGroup   = i;
                s->bMuting      = false;
                s->bNoteOff     = false;

                // Initialize channels
                lsp_trace("Initializing channel group #%d...", int(i));
                for (size_t j=0; j<meta::sampler_metadata::TRACKS_MAX; ++j)
                {
                    sampler_channel_t *c    = &s->vChannels[j];
                    c->vDry     = NULL;
                    c->vListen  = NULL;
                    c->fPan     = 1.0f;
                    c->pDry     = NULL;
                    c->pPan     = NULL;
                }

                // Cleanup gain pointer
                s->pGain        = NULL;
                s->pBypass      = NULL;
                s->pDryBypass   = NULL;
                s->pChannel     = NULL;
                s->pNote        = NULL;
                s->pOctave      = NULL;
                s->pMuteGroup   = NULL;
                s->pMuting      = NULL;
                s->pMidiNote    = NULL;
                s->pNoteOff     = NULL;
            }

            // Initialize temporary buffers
            size_t allocate         = meta::sampler_metadata::BUFFER_SIZE * nChannels * 3; // vTmpIn + vTmpOut + vTmpListen
            lsp_trace("Allocating temporary buffer of %d samples", int(allocate));
            pBuffer                 = new float[allocate];
            if (pBuffer == NULL)
                return;

            lsp_trace("Initializing temporary buffers");
            float *fptr             = pBuffer;
            for (size_t i=0; i<nChannels; ++i)
            {
                vChannels[i].vTmpIn     = advance_ptr<float>(fptr, meta::sampler_metadata::BUFFER_SIZE);
                vChannels[i].vTmpOut    = advance_ptr<float>(fptr, meta::sampler_metadata::BUFFER_SIZE);
                vChannels[i].vTmpListen = advance_ptr<float>(fptr, meta::sampler_metadata::BUFFER_SIZE);
            }

            // Initialize metadata
            size_t port_id          = 0;

            // Bind audio inputs
            lsp_trace("Binding audio inputs...");
            for (size_t i=0; i<nChannels; ++i)
            {
                BIND_PORT(vChannels[i].pIn);
                vChannels[i].vIn        = NULL;
            }

            // Bind audio outputs
            lsp_trace("Binding audio outputs...");
            for (size_t i=0; i<nChannels; ++i)
            {
                BIND_PORT(vChannels[i].pOut);
                vChannels[i].vOut       = NULL;
            }

            // Bind MIDI ports
            lsp_trace("Binding MIDI ports...");
            BIND_PORT(pMidiIn);
            BIND_PORT(pMidiOut);

            // Bind ports
            lsp_trace("Binding Global ports...");
            BIND_PORT(pBypass);
            BIND_PORT(pMute);
            BIND_PORT(pMuting);
            BIND_PORT(pNoteOff);
            BIND_PORT(pFadeout);
            BIND_PORT(pDry);
            BIND_PORT(pWet);
            BIND_PORT(pDryWet);
            BIND_PORT(pGain);
            SKIP_PORT("Sample editor tab selector"); // Skip sample editor tab selector
            if (bDryPorts)
            {
                BIND_PORT(pDOGain);
                BIND_PORT(pDOPan);
            }

            // If number of samplers <= 2 - skip area selector
            if (nSamplers > 2)
            {
                lsp_trace("Skipping mixer selector port...");
                SKIP_PORT("Mixer selector");
            }

            // If number of samplers > 0 - skip instrument selector
            if (nSamplers > 1)
            {
                lsp_trace("Skipping instrument selector...");
                SKIP_PORT("Instrument selector");
            }

            // Now process each instrument
            for (size_t i=0; i<nSamplers; ++i)
            {
                sampler_t *s    = &vSamplers[i];

                // Bind trigger
                lsp_trace("Binding trigger #%d ports...", int(i));
                BIND_PORT(s->pChannel);
                BIND_PORT(s->pNote);
                BIND_PORT(s->pOctave);
                if (nSamplers > 1)
                {
                    BIND_PORT(s->pMuteGroup);
                    BIND_PORT(s->pMuting);
                    BIND_PORT(s->pNoteOff);
                }
                BIND_PORT(s->pMidiNote);

                // Bind sampler
                lsp_trace("Binding sampler #%d ports...", int(i));
                s->sSampler.bind(ports, port_id, true);
            }

            if (nSamplers > 1)
            {
                for (size_t i=0; i<nSamplers; ++i)
                {
                    sampler_t *s = &vSamplers[i];

                    // Bind Bypass port
                    lsp_trace("Binding bypass port...");
                    BIND_PORT(s->pBypass);

                    // Bind mixing gain port
                    lsp_trace("Binding gain port...");
                    BIND_PORT(s->pGain);

                    // Bind panorama port
                    if (nChannels > 1)
                    {
                        lsp_trace("Binding panorama ports...");
                        for (size_t j=0; j<nChannels; ++j)
                            BIND_PORT(s->vChannels[j].pPan);
                    }

                    // Bind activity port
                    s->sSampler.bind_activity(ports, port_id);

                    // Bind dry port if present
                    if (bDryPorts)
                    {
                        lsp_trace("Binding dry ports...");
                        BIND_PORT(s->pDryBypass);

                        for (size_t j=0; j<nChannels; ++j)
                            BIND_PORT(s->vChannels[j].pDry);
                    }
                }
            }

            // Call for initial settings update
            lsp_trace("Calling settings update");
            update_settings();
        }

        uint32_t sampler::select_channels(size_t index)
        {
            if (index == meta::sampler_metadata::CHANNEL_ALL)
                return 0xffff;
            return 1 << index;
        }

        void sampler::destroy()
        {
            plug::Module::destroy();
        }

        void sampler::do_destroy()
        {
            if (vSamplers != NULL)
            {
                for (size_t i=0; i<nSamplers; ++i)
                {
                    sampler_t *s    = &vSamplers[i];
                    s->sSampler.destroy();

                    for (size_t j=0; j<nChannels; ++j)
                    {
                        sampler_channel_t *c    = &s->vChannels[j];
                        c->vDry         = NULL;
                        c->vListen      = NULL;
                        c->pDry         = NULL;
                        c->pPan         = NULL;
                    }

                    s->pGain        = NULL;
                    s->pBypass      = NULL;
                    s->pDryBypass   = NULL;
                    s->pChannel     = NULL;
                    s->pNote        = NULL;
                    s->pOctave      = NULL;
                    s->pMidiNote    = NULL;
                }

                delete [] vSamplers;
                vSamplers       = NULL;
            }

            if (pBuffer != NULL)
            {
                delete      [] pBuffer;
                pBuffer     = NULL;

                for (size_t i=0; i<meta::sampler_metadata::TRACKS_MAX; ++i)
                {
                    channel_t *tc   = &vChannels[i];
                    tc->vIn         = NULL;
                    tc->vOut        = NULL;
                    tc->vTmpIn      = NULL;
                    tc->vTmpOut     = NULL;
                    tc->vTmpListen  = NULL;
                    tc->pIn         = NULL;
                    tc->pOut        = NULL;
                }
            }
        }

        void sampler::update_settings()
        {
            // Update dry & wet parameters
            const float dry   = (pDry != NULL)    ? pDry->value()  : 1.0f;
            const float wet   = (pWet != NULL)    ? pWet->value()  : 1.0f;
            const float drywet= (pDryWet != NULL) ? pDryWet->value() * 0.01f : 1.0f;
            const float gain  = (pGain != NULL)   ? pGain->value() : 1.0f;

            fDry        = (dry * drywet + 1.0f - drywet) * gain;
            fWet        = (wet * drywet) * gain;

            lsp_trace("dry = %f, wet=%f, gain=%f", dry, wet, gain);

            // Update muting state
            if (pMute != NULL)
                sMute.submit(pMute->value());

            // Update bypass (if present)
            if (pBypass != NULL)
            {
                bool bypass     = pBypass->value() >= 0.5f;
                for (size_t i=0; i<nChannels; ++i)
                    vChannels[i].sBypass.set_bypass(bypass);
            }

            // Update settings on all samplers and triggers
            bMuting         = pMuting->value() >= 0.5f;
            bool note_off   = pNoteOff->value() >= 0.5f;
            nDOMode         = 0;
            if ((pDOGain != NULL) && (pDOGain->value() >= 0.5f))
                nDOMode        |= DM_APPLY_GAIN;
            if ((pDOPan != NULL) && (pDOPan->value() >= 0.5f))
                nDOMode        |= DM_APPLY_PAN;

            lsp_trace("muting=%s", (bMuting) ? "true" : "false");
            lsp_trace("note_off=%s", (note_off) ? "true" : "false");
            lsp_trace("do_mode=0x%x", int(nDOMode));

            for (size_t i=0; i<nSamplers; ++i)
            {
                sampler_t *s    = &vSamplers[i];

                // MIDI note and channel
                s->nNote        = (s->pOctave->value() * 12) + s->pNote->value();
                s->nChannelMap  = select_channels(s->pChannel->value());
                s->nMuteGroup   = (s->pMuteGroup != NULL) ? s->pMuteGroup->value() : i;
                s->bMuting      = (s->pMuting != NULL) ? s->pMuting->value() >= 0.5f : bMuting;
                s->bNoteOff     = (s->pNoteOff != NULL) ? s->pNoteOff->value() >= 0.5f : false;
                s->bNoteOff     = s->bNoteOff || note_off;

//                lsp_trace("Sampler %d channels=0x%04x, note=%d", int(i), int(s->nChannelMap), int(s->nNote));
                if (s->pMidiNote != NULL)
                    s->pMidiNote->set_value(s->nNote);

                // Get gain values
                s->fGain        = (s->pGain != NULL) ? s->pGain->value() : 1.0f;
                if (nChannels <= 2)
                {
                    sampler_channel_t *c    = &s->vChannels[0];
                    c->fPan                 = (c->pPan != NULL) ? ((100.0f - c->pPan->value()) * 0.005f) : 1.0f;
                    if (nChannels  == 2)
                    {
                        c                       = &s->vChannels[1];
                        c->fPan                 = (c->pPan != NULL) ? ((100.0f + c->pPan->value()) * 0.005f) : 1.0f;
                    }
                }
                else
                {
                    for (size_t j=0; j<nChannels; ++j)
                    {
                        sampler_channel_t *c    = &s->vChannels[j];
                        c->fPan                 = (c->pPan != NULL) ? ((100.0f - c->pPan->value()) * 0.005f) : 1.0f;
                    }
                }

                // Get bypass
                bool bypass     = (s->pBypass != NULL) ? s->pBypass->value() < 0.5f : 0.0f;
                bool dry_bypass = (s->pDryBypass != NULL) ? s->pDryBypass->value() < 0.5f : 0.0f;
                for (size_t j=0; j<nChannels; ++j)
                {
                    sampler_channel_t *c    = &s->vChannels[j];
                    c->sBypass.set_bypass(bypass);
                    c->sDryBypass.set_bypass(dry_bypass);
                }

                // Additional parameters
                s->sSampler.set_fadeout(pFadeout->value());
                s->sSampler.update_settings();
            }
        }

        void sampler::update_sample_rate(long sr)
        {
            // Update sample rate for bypass
            for (size_t i=0; i<nChannels; ++i)
                vChannels[i].sBypass.init(sr);

            // Update settings on all samplers
            for (size_t i=0; i<nSamplers; ++i)
            {
                sampler_t *s = &vSamplers[i];
                s->sSampler.update_sample_rate(sr);

                for (size_t j=0; j<nChannels; ++j)
                {
                    sampler_channel_t *sc   = &s->vChannels[j];
                    sc->sBypass.init(sr);
                    sc->sDryBypass.init(sr);
                }
            }
        }

        void sampler::ui_activated()
        {
            // Update settings on all samplers
            for (size_t i=0; i<nSamplers; ++i)
            {
                sampler_t *s = &vSamplers[i];
                s->sSampler.sync_samples_with_ui();
            }
        }

        void sampler::process_trigger_events()
        {
            // Process muting button
            if ((pMute != NULL) && (sMute.pending()))
            {
                // Cancel playback for all samplers
                for (size_t i=0; i<nSamplers; ++i)
                    vSamplers[i].sSampler.trigger_cancel(0);
                sMute.commit(true);
            }

            // Get MIDI input, return if none
            plug::midi_t *in    = (pMidiIn != NULL) ? pMidiIn->buffer<plug::midi_t>() : NULL;
            if (in == NULL)
                return;

            // Bypass MIDI events
            plug::midi_t *out   = (pMidiOut != NULL) ? pMidiOut->buffer<plug::midi_t>() : NULL;
            if (out != NULL)
                out->copy_from(in);

            #ifdef LSP_TRACE
                if (in->nEvents > 0)
                    lsp_trace("trigger this=%p, number of events = %d", this, int(in->nEvents));
            #endif

            // Process MIDI events for all samplers
            for (size_t i=0; i<in->nEvents; ++i)
            {
                // Analyze MIDI event
                const midi::event_t *me     = &in->vEvents[i];
                switch (me->type)
                {
                    case midi::MIDI_MSG_NOTE_ON:
                    {
                        lsp_trace("NOTE_ON: channel=%d, pitch=%d, velocity=%d",
                                int(me->channel), int(me->note.pitch), int(me->note.velocity));

                        uint32_t mg[BITMASK_MAX]; // Triggered mute groups
                        uint32_t ts[BITMASK_MAX]; // Triggered samplers

                        // Initialize parameters
                        for (size_t j=0; j<BITMASK_MAX; ++j)
                        {
                            mg[j]       = 0;
                            ts[j]       = 0;
                        }

                        // Scan state of samplers
                        for (size_t j=0; j<nSamplers; ++j)
                        {
                            sampler_t *s = &vSamplers[j];
                            if ((me->note.pitch != s->nNote) || (!(s->nChannelMap & (1 << me->channel))))
                                continue;

                            size_t g    = s->nMuteGroup;
                            mg[g >> 5] |= (1 << (g & 0x1f));        // Mark mute group as triggered
                            ts[j >> 5] |= (1 << (j & 0x1f));        // Mark sampler as triggered
                        }

                        // Apply changes
                        for (size_t j=0; j<nSamplers; ++j)
                        {
                            sampler_t *s    = &vSamplers[j];
                            size_t g        = s->nMuteGroup;
                            bool muted      = (g > 0) && (mg[g >> 5] & (1 << (g & 0x1f)));
                            bool triggered  = ts[j >> 5] & (1 << (j & 0x1f));

                            if (triggered)
                                s->sSampler.trigger_on(me->timestamp, me->note.velocity);
                            else if (muted)
                                s->sSampler.trigger_cancel(me->timestamp);
                        }
                        break;
                    }

                    case midi::MIDI_MSG_NOTE_OFF:
                    {
                        lsp_trace("NOTE_OFF: channel=%d, pitch=%d, velocity=%d",
                            int(me->channel), int(me->note.pitch), int(me->note.velocity));

                        for (size_t j=0; j<nSamplers; ++j)
                        {
                            sampler_t *s = &vSamplers[j];
                            if ((me->note.pitch != s->nNote) || (!(s->nChannelMap & (1 << me->channel))))
                                continue;

                            if (s->bMuting)
                                s->sSampler.trigger_cancel(me->timestamp);
                            else
                                s->sSampler.trigger_off(me->timestamp, s->bNoteOff);
                        }
                        break;
                    }

                    case midi::MIDI_MSG_NOTE_CONTROLLER:
                        lsp_trace("NOTE_CONTROLLER: channel=%d, control=%02x, value=%d",
                            int(me->channel), int(me->ctl.control), int(me->ctl.value));
                        if (me->ctl.control != midi::MIDI_CTL_ALL_NOTES_OFF)
                            break;

                        for (size_t j=0; j<nSamplers; ++j)
                        {
                            sampler_t *s = &vSamplers[j];
                            if (!(s->nChannelMap & (1 << me->channel)))
                                continue;
                            bool muting = s->bMuting || bMuting;
                            if (!muting)
                                continue;

                            s->sSampler.trigger_cancel(me->timestamp);
                        }
                        break;

                    default:
                        break;
                }
            } // for i
        }

        void sampler::process(size_t samples)
        {
            // Process all MIDI events
            process_trigger_events();

            // Prepare audio channels
            for (size_t i=0; i<nChannels; ++i)
            {
                channel_t *c    = &vChannels[i];
                c->vIn          = c->pIn->buffer<float>();
                c->vOut         = c->pOut->buffer<float>();
            }

            // Prepare sampler's buffers
            float *tmp_outs[meta::sampler_metadata::TRACKS_MAX];
            float *tmp_listen[meta::sampler_metadata::TRACKS_MAX];
            const float *tmp_ins[meta::sampler_metadata::TRACKS_MAX];

            for (size_t i=0; i<nChannels; ++i)
            {
                tmp_ins[i]      = NULL;
                tmp_outs[i]     = vChannels[i].vTmpOut;
                tmp_listen[i]   = vChannels[i].vTmpListen;

                // Bind direct channels (if present)
                for (size_t j=0; j<nSamplers; ++j)
                {
                    sampler_t *s            = &vSamplers[j];
                    sampler_channel_t *c    = &s->vChannels[i];
                    c->vDry                 = (c->pDry != NULL) ? c->pDry->buffer<float>() : NULL;
                }
            }

            // Process samples
            for (size_t offset = 0; offset < samples; )
            {
                // Determine number of elements to process
                const size_t count      = lsp_min(samples - offset, meta::sampler_metadata::BUFFER_SIZE);

                // Save input data into temporary input buffer
                for (size_t i=0; i<nChannels; ++i)
                {
                    channel_t *c            = &vChannels[i];
                    dsp::copy(c->vTmpIn, c->vIn, count);
                    dsp::fill_zero(c->vOut, count);
                }

                // Execute all samplers
                for (size_t i=0; i<nSamplers; ++i)
                {
                    sampler_t *s = &vSamplers[i];

                    // Call sampler for processing
                    s->sSampler.process(tmp_listen, tmp_outs, tmp_ins, count);

                    // Preprocess dry channels: fill with zeros
                    for (size_t j=0; j<nChannels; ++j)
                    {
                        sampler_channel_t *c    = &s->vChannels[j];
                        if (c->vDry != NULL)
                            dsp::fill_zero(c->vDry, count);
                    }

                    // Now post-process all channels for sampler
                    for (size_t j=0; j<nChannels; ++j)
                    {
                        sampler_channel_t *c    = &s->vChannels[j];

                        // Copy data to direct output buffer if present
                        float gain  = (nDOMode & DM_APPLY_GAIN) ? s->fGain : 1.0f;
                        float pan   = (nDOMode & DM_APPLY_PAN) ? c->fPan : 1.0f;
                        if (s->vChannels[j].vDry != NULL)
                            dsp::fmadd_k3(s->vChannels[j].vDry, tmp_outs[j], pan * gain, count);
                        if (s->vChannels[j^1].vDry != NULL)
                            dsp::fmadd_k3(s->vChannels[j^1].vDry, tmp_outs[j], (1.0f - pan) * gain, count);

                        // Process output
                        c->sBypass.process(tmp_outs[j], NULL, tmp_outs[j], count);
                        dsp::add2(tmp_outs[j], tmp_listen[j], count);

                        // Mix output to common sampler's bus
                        if (vChannels[j].vOut != NULL)
                            dsp::fmadd_k3(vChannels[j].vOut, tmp_outs[j], c->fPan * s->fGain, count);

                        // Apply pan to the other stereo channel (if present)
                        if (vChannels[j^1].vOut != NULL)
                            dsp::fmadd_k3(vChannels[j^1].vOut, tmp_outs[j], (1.0f - c->fPan) * s->fGain, count);
                    }

                    // Post-process dry channels
                    for (size_t j=0; j<nChannels; ++j)
                    {
                        sampler_channel_t *c    = &s->vChannels[j];
                        if (c->vDry != NULL)
                        {
                            c->sDryBypass.process(c->vDry, NULL, c->vDry, count);
                            c->vDry    += count;
                        }
                    }
                }

                // Post-process the summarized signal from samplers
                for (size_t i=0; i<nChannels; ++i)
                {
                    channel_t *c            = &vChannels[i];

                    dsp::mix2(c->vOut, c->vTmpIn, fWet, fDry, count); // Adjust volume between dry and wet channels
                    if (pBypass != NULL)
                        c->sBypass.process(c->vOut, c->vTmpIn, c->vOut, count);

                    // Increment pointers
                    c->vOut                += count;
                    c->vIn                 += count;
                }

                // Decrement counter
                offset                 += count;
            }
        }

        void sampler::dump_sampler(dspu::IStateDumper *v, const sampler_t *s) const
        {
            v->write_object("sSampler", &s->sSampler);

            v->write("fGain", s->fGain);
            v->write("nNote", s->nNote);
            v->write("nChannelMap", s->nChannelMap);
            v->write("nMuteGroup", s->nMuteGroup);
            v->write("bMuting", s->bMuting);
            v->write("bNoteOff", s->bNoteOff);

            v->begin_array("vChannels", vChannels, nChannels);
            {
                for (size_t i=0; i<nChannels; ++i)
                {
                    const sampler_channel_t *c = &s->vChannels[i];

                    v->begin_object(c, sizeof(sampler_channel_t));
                    {
                        v->write("vDry", c->vDry);
                        v->write("fPan", c->fPan);
                        v->write_object("sBypass", &c->sBypass);
                        v->write_object("sDryBypass", &c->sDryBypass);
                        v->write("pDry", c->pDry);
                        v->write("pPan", c->pPan);
                    }
                    v->end_object();
                }
            }
            v->end_array();

            v->write("pGain", s->pGain);
            v->write("pBypass", s->pBypass);
            v->write("pDryBypass", s->pDryBypass);
            v->write("pChannel", s->pChannel);
            v->write("pNote", s->pNote);
            v->write("pOctave", s->pOctave);
            v->write("pMuteGroup", s->pMuteGroup);
            v->write("pMuting", s->pMuting);
            v->write("pMidiNote", s->pMidiNote);
            v->write("pNoteOff", s->pNoteOff);
        }

        void sampler::dump_channel(dspu::IStateDumper *v, const channel_t *s) const
        {
            v->write("vIn", s->vIn);
            v->write("vOut", s->vOut);
            v->write("vTmpIn", s->vTmpIn);
            v->write("vTmpOut", s->vTmpOut);
            v->write("vTmpListen", s->vTmpListen);
            v->write_object("sBypass", &s->sBypass);
            v->write("pIn", s->pIn);
            v->write("pOut", s->pOut);
        }

        void sampler::dump(dspu::IStateDumper *v) const
        {
            v->write("nChannels", nChannels);
            v->write("nSamplers", nSamplers);
            v->write("nFiles", nFiles);
            v->write("nDOMode", nDOMode);
            v->write("bDryPorts", bDryPorts);

            v->begin_array("vSamplers", vSamplers, nSamplers);
            {
                for (size_t i=0; i<nSamplers; ++i)
                {
                    v->begin_object(&vSamplers[i], sizeof(sampler_t));
                        dump_sampler(v, &vSamplers[i]);
                    v->end_object();
                }
            }
            v->end_array();

            v->begin_array("vChannels", vChannels, meta::sampler_metadata::TRACKS_MAX);
            {
                for (size_t i=0; i<meta::sampler_metadata::TRACKS_MAX; ++i)
                {
                    v->begin_object(&vChannels[i], sizeof(channel_t));
                        dump_channel(v, &vChannels[i]);
                    v->end_object();
                }
            }
            v->end_array();

            v->write_object("sMute", &sMute);

            v->write("pBuffer", pBuffer);
            v->write("fDry", fDry);
            v->write("fWet", fWet);
            v->write("bMuting", bMuting);

            v->write("pMidiIn", pMidiIn);
            v->write("pMidiOut", pMidiOut);

            v->write("pBypass", pBypass);
            v->write("pMute", pMute);
            v->write("pMuting", pMuting);
            v->write("pNoteOff", pNoteOff);
            v->write("pFadeout", pFadeout);
            v->write("pDry", pDry);
            v->write("pWet", pWet);
            v->write("pDryWet", pDryWet);
            v->write("pGain", pGain);
            v->write("pDOGain", pDOGain);
            v->write("pDOPan", pDOPan);
        }

    } /* namespace plugins */
} /* namespace lsp */



