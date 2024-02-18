/*
 * Copyright (C) 2024 Linux Studio Plugins Project <https://lsp-plug.in/>
 *           (C) 2024 Vladimir Sadovnikov <sadko4u@gmail.com>
 *
 * This file is part of lsp-plugins-sampler
 * Created on: 16 июл. 2021 г.
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

#ifndef PRIVATE_UI_SAMPLER_H_
#define PRIVATE_UI_SAMPLER_H_

#include <lsp-plug.in/plug-fw/ui.h>
#include <lsp-plug.in/fmt/Hydrogen.h>

#include <private/ui/sfz.h>

namespace lsp
{
    namespace plugui
    {
        class sampler_ui: public ui::Module, public ui::IPortListener
        {
            protected:
                enum h2drumkit_type_t
                {
                    H2DRUMKIT_SYSTEM,       // Installed in system paths
                    H2DRUMKIT_USER,         // Installed in user directory
                    H2DRUMKIT_CUSTOM,       // Custom defined path
                };

                typedef struct h2drumkit_t
                {
                    LSPString           sName;      // Name of the drumkit
                    io::Path            sBase;      // Base path
                    io::Path            sPath;      // Path to the drumkit, contains base path
                    h2drumkit_type_t    enType;     // System directory
                    tk::MenuItem       *pMenu;      // Corresponding menu item
                } h2drumkit_t;

                typedef struct inst_name_t
                {
                    tk::Edit           *pWidget;    // Pointer to the widget
                    size_t              nIndex;     // Instrument number
                    bool                bChanged;   // Change flag
                } inst_name_t;

                class BundleSerializer: public config::Serializer
                {
                    private:
                        sampler_ui                 *pUI;
                        lspc::File                 *pFD;
                        lltl::phashset<char>        vEntries;
                        lltl::pphash<char, char>    vFiles;

                    protected:
                        const char *make_bundle_path(const char *realpath);

                    public:
                        explicit BundleSerializer(sampler_ui *ui, lspc::File *fd);
                        virtual ~BundleSerializer();

                    public:
                        virtual status_t    write_string(const LSPString *key, const LSPString *v, size_t flags) override;
                        virtual status_t    write_string(const LSPString *key, const char *v, size_t flags) override;
                        virtual status_t    write_string(const char *key, const LSPString *v, size_t flags) override;
                        virtual status_t    write_string(const char *key, const char *v, size_t flags) override;
                };

                class BundleDeserializer: public config::PullParser
                {
                    private:
                        sampler_ui                 *pUI;
                        const io::Path             *pPath;

                    protected:
                        virtual status_t    commit_param(const LSPString *key, const LSPString *value, size_t flags) override;

                    public:
                        explicit BundleDeserializer(sampler_ui *ui, const io::Path *path);
                };

            protected:
                bool                        bMultiple;              // Multiple instruments
                ui::IPort                  *pHydrogenPath;
                ui::IPort                  *pHydrogenFileType;
                ui::IPort                  *pBundlePath;
                ui::IPort                  *pBundleFileType;
                ui::IPort                  *pSfzPath;
                ui::IPort                  *pSfzFileType;
                ui::IPort                  *pHydrogenCustomPath;    // Custom Hydrogen path
                ui::IPort                  *pCurrentInstrument;     // Name that holds number of current instrument
                tk::FileDialog             *wHydrogenImport;        // Hyrdogen file import dialog
                tk::FileDialog             *wSfzImport;             // SFZ file import dialog
                tk::FileDialog             *wBundleDialog;
                tk::MessageBox             *wMessageBox;
                tk::Edit                   *wCurrentInstrument;     // Name of the current instrument
                lltl::parray<tk::Widget>    vHydrogenMenus;
                lltl::parray<h2drumkit_t>   vDrumkits;
                lltl::darray<inst_name_t>   vInstNames; // Names of instruments

            protected:
                static status_t     slot_start_import_hydrogen_file(tk::Widget *sender, void *ptr, void *data);
                static status_t     slot_import_hydrogen_file(tk::Widget *sender, void *ptr, void *data);
                static status_t     slot_call_import_hydrogen_file(tk::Widget *sender, void *ptr, void *data);
                static status_t     slot_fetch_hydrogen_path(tk::Widget *sender, void *ptr, void *data);
                static status_t     slot_commit_hydrogen_path(tk::Widget *sender, void *ptr, void *data);
                static status_t     slot_instrument_name_updated(tk::Widget *sender, void *ptr, void *data);

                static status_t     slot_start_import_sfz_file(tk::Widget *sender, void *ptr, void *data);
                static status_t     slot_call_import_sfz_file(tk::Widget *sender, void *ptr, void *data);
                static status_t     slot_fetch_sfz_path(tk::Widget *sender, void *ptr, void *data);
                static status_t     slot_commit_sfz_path(tk::Widget *sender, void *ptr, void *data);

                static status_t     slot_start_export_sampler_bundle(tk::Widget *sender, void *ptr, void *data);
                static status_t     slot_start_import_sampler_bundle(tk::Widget *sender, void *ptr, void *data);
                static status_t     slot_fetch_sampler_bundle_path(tk::Widget *sender, void *ptr, void *data);
                static status_t     slot_commit_sampler_bundle_path(tk::Widget *sender, void *ptr, void *data);
                static status_t     slot_call_process_sampler_bundle(tk::Widget *sender, void *ptr, void *data);

                static ssize_t      cmp_drumkit_files(const h2drumkit_t *a, const h2drumkit_t *b);
                static ssize_t      cmp_sfz_regions(const sfz_region_t *a, const sfz_region_t *b);

                static status_t     allocate_temp_file(io::Path *dst, const io::Path *src);
                static status_t     slot_close_message_box(tk::Widget *sender, void *ptr, void *data);

            protected:
                status_t            read_path(io::Path *dst, const char *port_id);
                status_t            import_hydrogen_file(const LSPString *path);
                status_t            try_override_hydrogen_file(const io::Path *base, const io::Path *relative);
                status_t            import_drumkit_file(const io::Path *base, const LSPString *path);
                status_t            import_sfz_file(const io::Path *base, const io::Path *path);
                status_t            add_sample(const io::Path *base, int id, int jd, const hydrogen::layer_t *layer);
                status_t            add_instrument(int id, const hydrogen::instrument_t *inst);
                void                set_float_value(float value, const char *fmt...);
                void                set_path_value(const char *path, const char *fmt...);
                void                set_instrument_name(core::KVTStorage *kvt, int id, const char *name);

                void                sync_hydrogen_files();
                void                lookup_hydrogen_files();
                void                destroy_hydrogen_menus();
                void                init_path(tk::Widget *sender, ui::IPort *path, ui::IPort *file_type);
                void                commit_path(tk::Widget *sender, ui::IPort *path, ui::IPort *file_type);
                status_t            scan_hydrogen_directory(const io::Path *path, h2drumkit_type_t type);
                status_t            add_drumkit(const io::Path *base, const io::Path *path, const hydrogen::drumkit_t *dk, h2drumkit_type_t type);

                status_t            export_sampler_bundle(const io::Path *path);
                status_t            import_sampler_bundle(const io::Path *path);
                tk::FileDialog     *get_bundle_dialog(bool import);

                void                show_message(const char *title, const char *message, const expr::Parameters *params);

            public:
                explicit sampler_ui(const meta::plugin_t *meta, bool multiple);
                virtual ~sampler_ui() override;

                virtual status_t    init(ui::IWrapper *wrapper, tk::Display *dpy) override;
                virtual void        destroy() override;

            public:
                virtual status_t    post_init() override;
                virtual void        idle() override;
                virtual void        kvt_changed(core::KVTStorage *storage, const char *id, const core::kvt_param_t *value) override;
                virtual void        notify(ui::IPort *port, size_t flags) override;
                virtual status_t    reset_settings() override;
        };
    } /* namespace plugui */
} /* namespace lsp */


#endif /* PRIVATE_UI_SAMPLER_H_ */
