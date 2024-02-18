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

#ifndef PRIVATE_UI_MIDICODE_H_
#define PRIVATE_UI_MIDICODE_H_

#include <lsp-plug.in/plug-fw/ui.h>

namespace lsp
{
    namespace plugui
    {
        namespace sampler
        {
            class MidiVelocityPort: public ui::ProxyPort
            {
                public:
                    MidiVelocityPort();
                    virtual ~MidiVelocityPort();

                public:
                    status_t            init(const char *prefix, ui::IPort *port);

                public: // ui::ProxyPort
                    virtual float       from_value(float value) override;
                    virtual float       to_value(float value) override;
            };

        } /* namespace sampler */
    } /* namespace plugui */
} /* namespace lsp */



#endif /* PRIVATE_UI_MIDICODE_H_ */
