/*
 * Copyright (C) 2024 Linux Studio Plugins Project <https://lsp-plug.in/>
 *           (C) 2024 Vladimir Sadovnikov <sadko4u@gmail.com>
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

#ifndef PRIVATE_PLUGINS_SAMPLER_H_
#define PRIVATE_PLUGINS_SAMPLER_H_

#include <lsp-plug.in/plug-fw/plug.h>
#include <lsp-plug.in/dsp-units/ctl/Toggle.h>
#include <lsp-plug.in/dsp-units/ctl/Bypass.h>
#include <lsp-plug.in/lltl/parray.h>
#include <lsp-plug.in/ipc/ITask.h>

#include <private/meta/sampler.h>
#include <private/plugins/sampler_kernel.h>

namespace lsp
{
    namespace plugins
    {
        /**
         * Sampler plugin
         */
        class sampler: public plug::Module
        {
            protected:
                static const size_t BITMASK_MAX        = ((meta::sampler_metadata::INSTRUMENTS_MAX + 31) >> 5);

            protected:
                enum dm_mode_t
                {
                    DM_APPLY_GAIN   = 1 << 0,
                    DM_APPLY_PAN    = 1 << 1
                };

                typedef struct sampler_channel_t
                {
                    float          *vDry;           // Dry output
                    float           fPan;           // Gain
                    dspu::Bypass    sBypass;        // Bypass
                    dspu::Bypass    sDryBypass;     // Dry channel bypass

                    plug::IPort    *pDry;           // Dry port
                    plug::IPort    *pPan;           // Gain output
                } sampler_channel_t;

                typedef struct channel_t
                {
                    float           *vIn;            // Input
                    float           *vOut;           // Output
                    float           *vTmpIn;         // Temporary input buffer
                    float           *vTmpOut;        // Temporary output buffer
                    dspu::Bypass     sBypass;        // Bypass

                    plug::IPort     *pIn;            // Input port
                    plug::IPort     *pOut;           // Output port
                } channel_t;

                typedef struct sampler_t
                {
                    sampler_kernel      sSampler;                                               // Sampler
                    float               fGain;                                                  // Overall gain
                    size_t              nNote;                                                  // Trigger note
                    uint32_t            nChannelMap;                                            // Channel mapping
                    size_t              nMuteGroup;                                             // Mute group
                    bool                bMuting;                                                // Muting flag
                    bool                bNoteOff;                                               // Handle note-off event

                    sampler_channel_t   vChannels[meta::sampler_metadata::TRACKS_MAX];          // Sampler output channels
                    plug::IPort        *pGain;                                                  // Gain output port
                    plug::IPort        *pBypass;                                                // Bypass port
                    plug::IPort        *pDryBypass;                                             // Dry bypass port
                    plug::IPort        *pChannel;                                               // Note port
                    plug::IPort        *pNote;                                                  // Note port
                    plug::IPort        *pOctave;                                                // Octave port
                    plug::IPort        *pMuteGroup;                                             // Mute group
                    plug::IPort        *pMuting;                                                // Muting
                    plug::IPort        *pMidiNote;                                              // Output midi note #
                    plug::IPort        *pNoteOff;                                               // Note off switch
                } sampler_t;

            protected:
                size_t              nChannels;          // Number of channels per output
                size_t              nSamplers;          // Number of samplers
                size_t              nFiles;             // Number of files per sampler
                size_t              nDOMode;            // Mode of direct output
                bool                bDryPorts;          // Dry ports allocated as temporary buffers
                sampler_t          *vSamplers;          // Lisf of samplers

                channel_t           vChannels[meta::sampler_metadata::TRACKS_MAX];              // Temporary buffers for processing
                dspu::Toggle        sMute;              // Mute request
                float              *pBuffer;            // Buffer data used by vChannels
                float               fDry;               // Dry amount
                float               fWet;               // Wet amount
                bool                bMuting;            // Global muting option

                plug::IPort        *pMidiIn;            // MIDI input port
                plug::IPort        *pMidiOut;           // MIDI output port

                plug::IPort        *pBypass;            // Bypass port
                plug::IPort        *pMute;              // Mute request port
                plug::IPort        *pMuting;            // MIDI muting
                plug::IPort        *pNoteOff;           // Note-off event handling
                plug::IPort        *pFadeout;           // Note-off fadeout
                plug::IPort        *pDry;               // Dry amount port
                plug::IPort        *pWet;               // Wet amount port
                plug::IPort        *pDryWet;            // Dry/Wet balance
                plug::IPort        *pGain;              // Output gain port
                plug::IPort        *pDOGain;            // Direct output gain flag
                plug::IPort        *pDOPan;             // Direct output panning flag

            protected:
                void            process_trigger_events();

                void            dump_sampler(dspu::IStateDumper *v, const sampler_t *s) const;
                void            dump_channel(dspu::IStateDumper *v, const channel_t *s) const;

                void            do_destroy();

            protected:
                static uint32_t select_channels(size_t index);

            public:
                explicit        sampler(const meta::plugin_t *metadata, size_t samplers, size_t channels, bool dry_ports);
                sampler(const sampler &) = delete;
                sampler(sampler &&) = delete;
                virtual        ~sampler() override;

                sampler & operator = (const sampler &) = delete;
                sampler & operator = (sampler &&) = delete;

            public:
                virtual void    init(plug::IWrapper *wrapper, plug::IPort **ports) override;
                virtual void    destroy() override;

                virtual void    update_settings() override;
                virtual void    update_sample_rate(long sr) override;
                virtual void    ui_activated() override;

                virtual void    process(size_t samples) override;

                virtual void    dump(dspu::IStateDumper *v) const override;
        };

    } /* namespace plugins */
} /* namespace lsp */


#endif /* PRIVATE_PLUGINS_SAMPLER_H_ */
