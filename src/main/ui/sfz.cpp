/*
 * Copyright (C) 2023 Linux Studio Plugins Project <https://lsp-plug.in/>
 *           (C) 2023 Vladimir Sadovnikov <sadko4u@gmail.com>
 *
 * This file is part of lsp-plugins-sampler
 * Created on: 2 мар. 2023 г.
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

#include <private/ui/sfz.h>
#include <lsp-plug.in/dsp-units/units.h>
#include <lsp-plug.in/fmt/sfz/IDocumentHandler.h>
#include <lsp-plug.in/fmt/sfz/parse.h>
#include <lsp-plug.in/lltl/phashset.h>
#include <lsp-plug.in/stdlib/string.h>

namespace lsp
{
    namespace plugui
    {
        // SFZ file handler
        class SFZHandler: public sfz::IDocumentHandler
        {
            protected:
                typedef struct region_t
                {
                    sfz_region_t       *region;
                    LSPString           basedir;
                } region_t;

            private:
                LSPString                   sFileName;
                lltl::parray<region_t>      vRegions;
                lltl::parray<sfz_region_t> *pRegions;
                lltl::phashset<char>        sSamples;
                io::Path                    sBaseDir;
                LSPString                   sDefaultPath;
                ssize_t                     nNoteOffset;
                ssize_t                     nOctaveOffset;

            public:
                SFZHandler()
                {
                    pRegions        = NULL;
                    nNoteOffset     = 0;
                    nOctaveOffset   = 0;
                }

                virtual ~SFZHandler() override
                {
                    // Destroy regions
                    for (size_t i=0, n=vRegions.size(); i<n; ++i)
                    {
                        region_t *r = vRegions.uget(i);
                        if (r != NULL)
                            delete r;
                    }
                    vRegions.flush();

                    // Destroy samples
                    lltl::parray<char> samples;
                    sSamples.values(&samples);
                    sSamples.flush();

                    for (size_t i=0, n=samples.size(); i<n; ++i)
                        free(samples.uget(i));
                }

            public:
                status_t init(lltl::parray<sfz_region_t> *out, const io::Path *file)
                {
                    status_t res;
                    pRegions        = out;

                    if ((res = file->get_parent(&sBaseDir)) != STATUS_OK)
                        return res;
                    if ((res = file->get_last(&sFileName)) != STATUS_OK)
                        return res;
                    if ((res = sBaseDir.get(&sDefaultPath)) != STATUS_OK)
                        return res;

                    return STATUS_OK;
                }

            public:
                virtual status_t control(const char **opcodes, const char **values) override
                {
                    status_t res;

                    for (;(opcodes != NULL) && (*opcodes != NULL); ++opcodes, ++values)
                    {
                        const char *opcode = *opcodes;
                        const char *value = *values;

                        if (!strcmp(opcode, "default_path"))
                        {
                            if (!sDefaultPath.set_utf8(value))
                                return STATUS_NO_MEM;
                        }
                        else if (!strcmp(opcode, "note_offset"))
                        {
                            if ((res = sfz::parse_int(&nNoteOffset, value)) != STATUS_OK)
                                return res;
                        }
                        else if (!strcmp(opcode, "octave_offset"))
                        {
                            if ((res = sfz::parse_int(&nOctaveOffset, value)) != STATUS_OK)
                                return res;
                        }
                    }

                    return STATUS_OK;
                }

                virtual status_t region(const char **opcodes, const char **values) override
                {
                    status_t res;

                    // Allocate output region
                    sfz_region_t *sr    = new sfz_region_t;
                    if (sr == NULL)
                        return STATUS_NO_MEM;
                    lsp_finally {
                        if (sr != NULL)
                            delete sr;
                    };

                    sr->flags           = 0;
                    sr->key             = 0;
                    sr->lokey           = 0;
                    sr->hikey           = 0;
                    sr->pitch_keycenter = 0;
                    sr->lovel           = 0;
                    sr->hivel           = 0;
                    sr->tune            = 0;
                    sr->volume          = 1.0f;

                    // Allocate region
                    region_t *r         = new region_t;
                    if (r == NULL)
                        return STATUS_NO_MEM;
                    lsp_finally {
                        if (r != NULL)
                            delete r;
                    };
                    r->region           = sr;
                    if (!r->basedir.set(&sDefaultPath))
                        return STATUS_NO_MEM;

                    // Process all opcodes
                    for (;(opcodes != NULL) && (*opcodes != NULL); ++opcodes, ++values)
                    {
                        const char *opcode = *opcodes;
                        const char *value = *values;

                        if (!strcmp(opcode, "sample"))
                        {
                            if (!sr->sample.set_utf8(value))
                                return STATUS_NO_MEM;
                            sr->flags      |= SFZ_SAMPLE;
                        }
                        else if (!strcmp(opcode, "group_label"))
                        {
                            if (!sr->sample.set_utf8(value))
                                return STATUS_NO_MEM;
                            sr->flags      |= SFZ_GROUP_LABEL;
                        }
                        else if (!strcmp(opcode, "key"))
                        {
                            if ((res = sfz::parse_note(&sr->key, value)) != STATUS_OK)
                                return res;
                            sr->flags      |= SFZ_KEY;
                        }
                        else if (!strcmp(opcode, "lokey"))
                        {
                            if ((res = sfz::parse_note(&sr->lokey, value)) != STATUS_OK)
                                return res;
                            sr->flags      |= SFZ_LOKEY;
                        }
                        else if (!strcmp(opcode, "hikey"))
                        {
                            if ((res = sfz::parse_note(&sr->hikey, value)) != STATUS_OK)
                                return res;
                            sr->flags      |= SFZ_HIKEY;
                        }
                        else if (!strcmp(opcode, "pitch_keycenter"))
                        {
                            if ((res = sfz::parse_int(&sr->pitch_keycenter, value)) != STATUS_OK)
                                return res;
                            sr->flags      |= SFZ_PITCH_KEYCENTER;
                        }
                        else if (!strcmp(opcode, "lovel"))
                        {
                            if ((res = sfz::parse_int(&sr->lovel, value)) != STATUS_OK)
                                return res;
                            sr->flags      |= SFZ_LOVEL;
                        }
                        else if (!strcmp(opcode, "hivel"))
                        {
                            if ((res = sfz::parse_int(&sr->hivel, value)) != STATUS_OK)
                                return res;
                            sr->flags      |= SFZ_HIVEL;
                        }
                        else if (!strcmp(opcode, "tune"))
                        {
                            if ((res = sfz::parse_int(&sr->tune, value)) != STATUS_OK)
                                return res;
                            sr->flags      |= SFZ_TUNE;
                        }
                        else if (!strcmp(opcode, "volume"))
                        {
                            if ((res = sfz::parse_float(&sr->volume, value)) != STATUS_OK)
                                return res;
                            sr->volume      = dspu::db_to_gain(sr->volume);
                            sr->flags      |= SFZ_VOLUME;
                        }
                    }

                    // Add region
                    if (!vRegions.add(r))
                        return STATUS_NO_MEM;
                    r       = NULL;

                    // Add region to list
                    if (!pRegions->add(sr))
                        return STATUS_NO_MEM;
                    sr      = NULL;

                    return STATUS_OK;
                }

                virtual status_t sample(
                    const char *name, io::IInStream *data,
                    const char **opcodes, const char **values) override
                {
                    char *sample_name = strdup(name);
                    if (sample_name == NULL)
                        return STATUS_NO_MEM;
                    lsp_finally { free(sample_name); };

                    return (sSamples.put(sample_name, &sample_name)) ? STATUS_OK : STATUS_NO_MEM;
                }

                virtual status_t include(sfz::PullParser *parser, const char *name) override
                {
                    io::Path child;
                    status_t res = child.set(&sBaseDir, name);
                    if (res != STATUS_OK)
                        return res;
                    return parser->open(&child);
                }

                virtual const char *root_file_name() override
                {
                    return sFileName.get_utf8();
                }

                virtual status_t end(status_t result) override
                {
                    status_t res;
                    if (result != STATUS_OK)
                        return STATUS_OK;

                    // Do sample post-processing
                    for (size_t i=0, n=vRegions.size(); i<n; ++i)
                    {
                        region_t *r = vRegions.uget(i);
                        if (r == NULL)
                            continue;
                        sfz_region_t *sr = pRegions->uget(i);
                        if (sr == NULL)
                            continue;

                        // Compute the full path to the sample
                        if (sSamples.contains(sr->sample.get_utf8()))
                        {
                            io::Path p;
                            if ((res = p.set(&sFileName, &sr->sample)) != STATUS_OK)
                                return res;
                            if ((res = p.get(&sr->sample)) != STATUS_OK)
                                return res;
                        }
                        else
                        {
                            if (!sr->sample.prepend(&r->basedir))
                                return STATUS_NO_MEM;
                        }

                        // Cleanup region reference
                        r->region       = NULL;
                    }

                    return STATUS_OK;
                }
        };


        status_t read_regions(lltl::parray<sfz_region_t> *list, const io::Path *file)
        {
            status_t res;
            SFZHandler handler;
            lltl::parray<sfz_region_t> tmp;
            lsp_finally { destroy_regions(&tmp); };
            sfz::DocumentProcessor processor;

            if ((res = handler.init(&tmp, file)) != STATUS_OK)
                return res;

            // Perform document processing
            if ((res = processor.open(file)) != STATUS_OK)
                return res;
            lsp_finally { processor.close(); };
            if ((res = processor.process(&handler)) != STATUS_OK)
                return res;
            if ((res = processor.close()) != STATUS_OK)
                return res;

            // Commit the result
            tmp.swap(list);

            return STATUS_OK;
        }

        void destroy_regions(lltl::parray<sfz_region_t> *list)
        {
            if (list == NULL)
                return;

            for (size_t i=0, n=list->size(); i<n; ++i)
            {
                sfz_region_t *r = list->uget(i);
                if (r != NULL)
                    delete r;
            }
            list->flush();
        }

    } /* namespace plugui */
} /* namespace lsp */


