/*
 * Copyright (C) 2023 Linux Studio Plugins Project <https://lsp-plug.in/>
 *           (C) 2023 Vladimir Sadovnikov <sadko4u@gmail.com>
 *
 * This file is part of lsp-plugins-sampler
 * Created on: 1 мар. 2023 г.
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

#ifndef PRIVATE_UI_SFZ_H_
#define PRIVATE_UI_SFZ_H_

#include <lsp-plug.in/common/types.h>
#include <lsp-plug.in/fmt/sfz/DocumentProcessor.h>
#include <lsp-plug.in/lltl/parray.h>

namespace lsp
{
    namespace plugui
    {
        enum sfz_region_flags_t
        {
            SFZ_SAMPLE          = 1 << 0,
            SFZ_KEY             = 1 << 1,
            SFZ_LOKEY           = 1 << 2,
            SFZ_HIKEY           = 1 << 3,
            SFZ_PITCH_KEYCENTER = 1 << 4,
            SFZ_LOVEL           = 1 << 5,
            SFZ_HIVEL           = 1 << 6,
            SFZ_TUNE            = 1 << 7,
            SFZ_VOLUME          = 1 << 8,
            SFZ_GROUP_LABEL     = 1 << 9,
        };

        // A single region that is read from SFZ
        typedef struct sfz_region_t
        {
            size_t              flags;              // The fields present in the structure
            LSPString           sample;             // The actual name of the sample file
            LSPString           group_label;        // The group label
            ssize_t             key;                // The MIDI note the region is assigned to
            ssize_t             lokey;              // The low MIDI note in the range the region is assigned to
            ssize_t             hikey;              // The high MIDI note in the range the region is assigned to
            ssize_t             pitch_keycenter;    // The default MIDI note in the range the region is assigned to
            ssize_t             lovel;              // The lowest velocity value
            ssize_t             hivel;              // The highest velocity value
            ssize_t             tune;               // The fine tune for the sample in cents (-100..100)
            float               volume;             // The volume for the region in dB
        } sfz_region_t;

        /**
         * Read all regions from the SFZ file
         * @param list list of regions to store the result
         * @param file the path to the file
         * @return status of operation
         */
        status_t read_regions(lltl::parray<sfz_region_t> *list, const io::Path *file);

        /**
         * Destroy all regions in the list
         * @param list list to destroy data
         */
        void destroy_regions(lltl::parray<sfz_region_t> *list);

    } /* namespace plugui */
} /* namespace lsp */



#endif /* PRIVATE_UI_SFZ_H_ */
