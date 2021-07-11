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

#include <private/plugins/sampler.h>

namespace lsp
{
    namespace plugins
    {
        //---------------------------------------------------------------------
        // Plugin factory
        typedef struct sampler_settings_t
        {
            const meta::plugin_t   *metadata;
            size_t                  samplers;
            size_t                  channels;
            bool                    dry_ports;
        } sampler_settings_t;

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

        static const sampler_settings_t sampler_settings[] =
        {
            { &meta::sampler_mono, 1, 1, false },
            { &meta::sampler_stereo, 1, 2, false },
            { &meta::multisampler_x12, 12, 2, false },
            { &meta::multisampler_x24, 24, 2, false },
            { &meta::multisampler_x48, 48, 2, false },
            { &meta::multisampler_x12_do, 12, 2, true },
            { &meta::multisampler_x24_do, 24, 2, true },
            { &meta::multisampler_x48_do, 48, 2, true },
            { NULL, 0, 0, false }
        };

        static plug::Module *spectrum_analyzer_factory(const meta::plugin_t *meta)
        {
            for (const sampler_settings_t *s = sampler_settings; s->metadata != NULL; ++s)
                if (!strcmp(s->metadata->uid, meta->uid))
                    return new sampler(s->metadata, s->samplers, s->channels, s->dry_ports);
            return NULL;
        }

        static plug::Factory factory(spectrum_analyzer_factory, plugins, 8);

        //-------------------------------------------------------------------------
        //
    }
}



