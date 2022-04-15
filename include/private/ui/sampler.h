/*
 * Copyright (C) 2021 Linux Studio Plugins Project <https://lsp-plug.in/>
 *           (C) 2021 Vladimir Sadovnikov <sadko4u@gmail.com>
 *
 * This file is part of lsp-plugin-fw
 * Created on: 16 июл. 2021 г.
 *
 * lsp-plugin-fw is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * lsp-plugin-fw is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with lsp-plugin-fw. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef LSP_PLUGINS_SAMPLER_INCLUDE_PRIVATE_UI_SAMPLER_H_
#define LSP_PLUGINS_SAMPLER_INCLUDE_PRIVATE_UI_SAMPLER_H_

#include <lsp-plug.in/plug-fw/ui.h>
#include <lsp-plug.in/fmt/Hydrogen.h>

namespace lsp
{
    namespace plugins
    {
        class sampler_ui: public ui::Module
        {
            protected:
                typedef struct h2drumkit_t
                {
                    LSPString           sName;      // Name of the drumkit
                    io::Path            sPath;      // Location of the drumkit
                    bool                bSystem;    // System directory
                    tk::MenuItem       *pMenu;      // Corresponding menu item
                } h2drumkit_t;

                typedef struct inst_name_t
                {
                    tk::Edit           *pWidget;    // Pointer to the widget
                    size_t              nIndex;     // Instrument number
                    bool                bChanged;   // Change flag
                } inst_name_t;

            protected:
                ui::IPort                  *pHydrogenPath;
                tk::FileDialog             *pHydrogenImport;
                lltl::parray<h2drumkit_t>   vDrumkits;
                lltl::darray<inst_name_t>   vInstNames; // Names of instruments

            protected:
                static status_t     slot_start_import_hydrogen_file(tk::Widget *sender, void *ptr, void *data);
                static status_t     slot_import_hydrogen_file(tk::Widget *sender, void *ptr, void *data);
                static status_t     slot_call_import_hydrogen_file(tk::Widget *sender, void *ptr, void *data);
                static status_t     slot_fetch_hydrogen_path(tk::Widget *sender, void *ptr, void *data);
                static status_t     slot_commit_hydrogen_path(tk::Widget *sender, void *ptr, void *data);
                static status_t     slot_instrument_name_updated(tk::Widget *sender, void *ptr, void *data);

                static ssize_t      cmp_drumkit_files(const h2drumkit_t *a, const h2drumkit_t *b);

            protected:
                status_t            import_hydrogen_file(const LSPString *path);
                status_t            add_sample(const io::Path *base, int id, int jd, const hydrogen::layer_t *layer);
                status_t            add_instrument(int id, const hydrogen::instrument_t *inst);
                void                set_float_value(float value, const char *fmt...);
                void                set_path_value(const char *path, const char *fmt...);

                void                lookup_hydrogen_files();
                void                add_hydrogen_files_to_menu(tk::Menu *menu);
                status_t            scan_hydrogen_directory(const io::Path *path, bool system);
                status_t            add_drumkit(const io::Path *path, const hydrogen::drumkit_t *dk, bool system);

            public:
                explicit sampler_ui(const meta::plugin_t *meta);
                virtual ~sampler_ui();

                virtual status_t    post_init();

                virtual void        idle();

                virtual void        kvt_changed(core::KVTStorage *storage, const char *id, const core::kvt_param_t *value);
        };
    } // namespace ui
} // namespace lsp


#endif /* LSP_PLUGINS_SAMPLER_INCLUDE_PRIVATE_UI_SAMPLER_H_ */
