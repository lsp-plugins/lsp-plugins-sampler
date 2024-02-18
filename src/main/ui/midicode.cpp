/*
 * Copyright (C) 2024 Linux Studio Plugins Project <https://lsp-plug.in/>
 *           (C) 2024 Vladimir Sadovnikov <sadko4u@gmail.com>
 *
 * This file is part of lsp-plugins-sampler
 * Created on: 18 февр. 2024 г.
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

#include <lsp-plug.in/plug-fw/meta/ports.h>
#include <lsp-plug.in/stdlib/string.h>

#include <private/ui/midicode.h>

namespace lsp
{
    namespace plugui
    {
        namespace sampler
        {
            static constexpr float midi_velocity_min = 0.0f;
            static constexpr float midi_velocity_max = 127.0f;
            static constexpr float midi_velocity_err = 0.01f;
            static constexpr float midi_velocity_rng = midi_velocity_max - midi_velocity_min;

            const meta::port_t midi_velocity_template =
            {
                "",
                "MIDI velocity",
                meta::U_NONE,
                meta::R_CONTROL,
                meta::F_INT | meta::F_LOWER | meta::F_UPPER | meta::F_STEP,
                midi_velocity_min, midi_velocity_max, 0.0f, 0.05f,
                NULL, NULL
            };

            MidiVelocityPort::MidiVelocityPort()
            {
            }

            MidiVelocityPort::~MidiVelocityPort()
            {
            }

            status_t MidiVelocityPort::init(const char *prefix, ui::IPort *port)
            {
                LSPString tmp;
                if (port == NULL)
                    return STATUS_BAD_ARGUMENTS;
                const meta::port_t *meta = port->metadata();
                if ((meta == NULL) || (meta->id == NULL))
                    return STATUS_INVALID_VALUE;

                const char *cptr = strchr(meta->id, '_');
                if (cptr == NULL)
                    return STATUS_INVALID_VALUE;

                if (!tmp.set_utf8(prefix))
                    return STATUS_NO_MEM;
                if (!tmp.append_utf8(cptr))
                    return STATUS_NO_MEM;

                return ProxyPort::init(tmp.get_utf8(), port, &midi_velocity_template);
            }

            float MidiVelocityPort::from_value(float value)
            {
                const meta::port_t *meta = proxied_metadata();
                if (meta == NULL)
                    return value;

                const float range = midi_velocity_rng / (meta->max - meta->min);
                return (value - meta->min) * range;
            }

            float MidiVelocityPort::to_value(float value)
            {
                const meta::port_t *meta = proxied_metadata();
                if (meta == NULL)
                    return value;

                const float range = (meta->max - meta->min + midi_velocity_err) / midi_velocity_rng;
                value = meta->min + (value - midi_velocity_min) * range;
                return lsp_limit(value, meta->min, meta->max);
            }

        } /* namespace sampler */
    } /* namespace plugui */
} /* namespace lsp */


