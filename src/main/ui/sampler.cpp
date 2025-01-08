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

#include <lsp-plug.in/common/debug.h>
#include <lsp-plug.in/dsp-units/units.h>
#include <lsp-plug.in/expr/EnvResolver.h>
#include <lsp-plug.in/fmt/lspc/lspc.h>
#include <lsp-plug.in/fmt/lspc/util.h>
#include <lsp-plug.in/fmt/url.h>
#include <lsp-plug.in/io/Dir.h>
#include <lsp-plug.in/plug-fw/ui.h>
#include <lsp-plug.in/plug-fw/meta/func.h>
#include <lsp-plug.in/runtime/system.h>

#include <private/plugins/sampler.h>
#include <private/ui/sampler.h>
#include <private/ui/sampler_midi.h>
#include <private/ui/sfz.h>

namespace lsp
{
    namespace plugui
    {
        //---------------------------------------------------------------------
        // Plugin UI factory
        static const meta::plugin_t *sampler_uis[] =
        {
            &meta::sampler_mono,
            &meta::sampler_stereo
        };

        static const meta::plugin_t *multisampler_uis[] =
        {
            &meta::multisampler_x12,
            &meta::multisampler_x24,
            &meta::multisampler_x48,
            &meta::multisampler_x12_do,
            &meta::multisampler_x24_do,
            &meta::multisampler_x48_do
        };

        static ui::Module *sampler_factory_func(const meta::plugin_t *meta)
        {
            return new sampler_ui(meta, false);
        }

        static ui::Module *multisampler_factory_func(const meta::plugin_t *meta)
        {
            return new sampler_ui(meta, true);
        }

        // Use different factories for Mono/Stereo sampler and multisampler plugins
        static ui::Factory sampler_factory(sampler_factory_func, sampler_uis, 2);
        static ui::Factory multisampler_factory(multisampler_factory_func, multisampler_uis, 6);

        //---------------------------------------------------------------------
        static const char * h2_system_paths[] =
        {
            "/usr/share/hydrogen",
            "/usr/local/share/hydrogen",
            "/opt/hydrogen",
            "/share/hydrogen",
            NULL
        };

        static const char * h2_user_paths[] =
        {
            ".hydrogen",
            ".h2",
            ".config/hydrogen",
            ".config/h2",
            NULL
        };

        //---------------------------------------------------------------------
        sampler_ui::BundleSerializer::BundleSerializer(sampler_ui *ui, lspc::File *fd)
        {
            pUI = ui;
            pFD = fd;
        }

        sampler_ui::BundleSerializer::~BundleSerializer()
        {
            lltl::parray<char> v;
            vEntries.values(&v);
            for (size_t i=0, n=v.size(); i<n; ++i)
            {
                char *str = v.uget(i);
                if (str != NULL)
                    free(str);
            }

            vFiles.flush();
            vEntries.flush();
        }

        status_t sampler_ui::BundleSerializer::write_string(const LSPString *key, const LSPString *v, size_t flags)
        {
            return write_string(key->get_utf8(), v->get_utf8(), flags);
        }

        status_t sampler_ui::BundleSerializer::write_string(const LSPString *key, const char *v, size_t flags)
        {
            return write_string(key->get_utf8(), v, flags);
        }

        status_t sampler_ui::BundleSerializer::write_string(const char *key, const LSPString *v, size_t flags)
        {
            return write_string(key, v->get_utf8(), flags);
        }

        const char *sampler_ui::BundleSerializer::make_bundle_path(const char *realpath)
        {
            // Try to fetch item
            const char *p = vFiles.get(realpath);
            if (p != NULL)
                return p;

            // Item not found, allocate new entry
            io::Path path, last;
            if (path.set(realpath) != STATUS_OK)
                return NULL;
            if (path.get_last(&last) != STATUS_OK)
                return NULL;

            // Generate unique path in the bundle
            LSPString bundle_path;
            for (int i=0; ; ++i)
            {
                if (bundle_path.fmt_utf8("%d/%s", i, last.as_utf8()) <= 0)
                    return NULL;
                if (!vEntries.contains(bundle_path.get_utf8()))
                    break;
            }

            // Now we can store the path in the collection
            char *result = bundle_path.clone_utf8();
            if (result == NULL)
                return NULL;
            if (!vEntries.put(result))
            {
                free(result);
                return NULL;
            }
            // Value `result` is stored in vEntries, we don't need to free() it anymore
            if (!vFiles.create(realpath, result))
                return NULL;
            return result;
        }

        status_t sampler_ui::BundleSerializer::write_string(const char *key, const char *v, size_t flags)
        {
            status_t res;

            // We need to translate paths only
            ui::IPort *port = pUI->wrapper()->port(key);
            if ((port == NULL) || (!meta::is_path_port(port->metadata())))
                return Serializer::write_string(key, v, flags);

            // Obtain the source path
            const char *source_path = port->buffer<char>();
            if (strlen(source_path) <= 0)
                return Serializer::write_string(key, v, flags);

            // Translate path in context of drumkit (exclude duplicate entry names)
            const char *bundle_path = make_bundle_path(source_path);
            if (bundle_path == NULL)
                return STATUS_NO_MEM;

            // Save audio to LSPC
            lspc::chunk_id_t audio_chunk;
            if ((res = lspc::write_audio(&audio_chunk, pFD, source_path)) == STATUS_OK)
            {
                // Save path
                if ((res = lspc::write_path(NULL, pFD, bundle_path, 0, audio_chunk)) != STATUS_OK)
                    return res;
                v   = bundle_path;
            }
            else
                v   = "";

            return Serializer::write_string(key, v, flags);
        }

        //---------------------------------------------------------------------
        sampler_ui::BundleDeserializer::BundleDeserializer(sampler_ui *ui, const io::Path *path)
        {
            pUI         = ui;
            pPath       = path;
        }

        status_t sampler_ui::BundleDeserializer::commit_param(const LSPString *key, const LSPString *value, size_t flags)
        {
            // We need to translate paths only
            ui::IPort *port = pUI->wrapper()->port(key);
            if ((port == NULL) || (!meta::is_path_port(port->metadata())) || (value->is_empty()))
                return PullParser::commit_param(key, value, flags);

            // Make path of the file based on the path of the whole bundle
            io::Path tmp;
            status_t res = tmp.set(pPath, value);
            if (res != STATUS_OK)
                return res;

            return PullParser::commit_param(key, tmp.as_string(), flags);
        }

        //-----------------------------------------------------------------
        sampler_ui::DragInSink::DragInSink(sampler_ui *ui)
        {
            pUI         = ui;
        }

        sampler_ui::DragInSink::~DragInSink()
        {
            unbind();
        }

        void sampler_ui::DragInSink::unbind()
        {
            if (pUI != NULL)
            {
                if (pUI->pDragInSink == this)
                    pUI->pDragInSink    = NULL;
                pUI = NULL;
            }
        }

        status_t sampler_ui::DragInSink::commit_url(const LSPString *url)
        {
            if (url == NULL)
                return STATUS_OK;

            LSPString decoded;
            status_t res = (url->starts_with_ascii("file://")) ?
                    url::decode(&decoded, url, 7) :
                    url::decode(&decoded, url);

            if (res != STATUS_OK)
                return res;

            pUI->handle_file_drop(&decoded);

            return STATUS_OK;
        }

        //---------------------------------------------------------------------
        sampler_ui::sampler_ui(const meta::plugin_t *meta, bool multiple):
            ui::Module(meta)
        {
            bMultiple           = multiple;
            pHydrogenPath       = NULL;
            pHydrogenFileType   = NULL;
            pBundlePath         = NULL;
            pBundleFileType     = NULL;
            pSfzPath            = NULL;
            pSfzFileType        = NULL;
            pHydrogenCustomPath = NULL;
            pCurrentInstrument  = NULL;
            pCurrentSample      = NULL;
            wHydrogenImport     = NULL;
            wSfzImport          = NULL;
            wBundleDialog       = NULL;
            wMessageBox         = NULL;
            wCurrentInstrument  = NULL;
            wInstrumentsGroup   = NULL;
            pDragInSink         = NULL;
        }

        sampler_ui::~sampler_ui()
        {
            // Will be automatically destroyed from list of widgets
            wHydrogenImport     = NULL;
            wSfzImport          = NULL;
            wBundleDialog       = NULL;
            wMessageBox         = NULL;
            wCurrentInstrument  = NULL;
            wInstrumentsGroup   = NULL;

            // Destroy instrument descriptors
            vInstNames.flush();
        }

        void sampler_ui::destroy()
        {
            // Destroy sink
            DragInSink *sink = pDragInSink;
            if (sink != NULL)
            {
                sink->unbind();
                sink->release();
                sink   = NULL;
            }

            // Destroy other stuff
            destroy_hydrogen_menus();
            ui::Module::destroy();
        }

        status_t sampler_ui::init(ui::IWrapper *wrapper, tk::Display *dpy)
        {
            status_t res = ui::Module::init(wrapper, dpy);
            if (res != STATUS_OK)
                return res;

            // Seek for all velocity ports and create proxy ports
            for (size_t i=0, n=wrapper->ports(); i<n; ++i)
            {
                ui::IPort *port = wrapper->port(i);
                if (port == NULL)
                    continue;
                const meta::port_t *meta = port->metadata();
                if ((meta == NULL) || (meta->id == NULL))
                    continue;
                if (strstr(meta->id, "vl_") != meta->id)
                    continue;

                // Create proxy port
                sampler_midi::MidiVelocityPort *velocity = new sampler_midi::MidiVelocityPort();
                if (velocity == NULL)
                    return STATUS_NO_MEM;
                if ((res = velocity->init("midivel", port)) != STATUS_OK)
                    return res;
                if ((res = pWrapper->bind_custom_port(velocity)) != STATUS_OK)
                {
                    delete velocity;
                    return res;
                }
            }

            return STATUS_OK;
        }

        status_t sampler_ui::post_init()
        {
            status_t res = ui::Module::post_init();
            if (res != STATUS_OK)
                return res;

            // Do not perform other initialization since it is a very simple single-instrument sampler
            if (!bMultiple)
                return STATUS_OK;

            // Find different paths
            pHydrogenPath           =  pWrapper->port(HYDROGEN_PATH_PORT);
            pHydrogenFileType       =  pWrapper->port(HYDROGEN_FTYPE_PORT);
            pBundlePath             =  pWrapper->port(LSPC_BUNDLE_PATH_PORT);
            pBundleFileType         =  pWrapper->port(LSPC_BUNDLE_FTYPE_PORT);
            pSfzPath                =  pWrapper->port(SFZ_PATH_PORT);
            pSfzFileType            =  pWrapper->port(SFZ_FTYPE_PORT);
            pHydrogenCustomPath     =  pWrapper->port(UI_USER_HYDROGEN_KIT_PATH_PORT);

            // Bind ports
            if (pHydrogenCustomPath != NULL)
                pHydrogenCustomPath->bind(this);

            // Find widget and port associated with the current selected instrument
            pCurrentInstrument  = wrapper()->port("inst");
            pCurrentSample      = wrapper()->port("ssel");
            wCurrentInstrument  = wrapper()->controller()->widgets()->get<tk::Edit>("iname");
            wInstrumentsGroup   = wrapper()->controller()->widgets()->get<tk::ComboGroup>("inst_cgroup");

            if (pCurrentInstrument != NULL)
                pCurrentInstrument->bind(this);
            if (wCurrentInstrument != NULL)
                wCurrentInstrument->slots()->bind(tk::SLOT_CHANGE, slot_instrument_name_updated, this);

            // Add subwidgets
            tk::Registry *widgets   = pWrapper->controller()->widgets();
            tk::Menu *menu          = tk::widget_cast<tk::Menu>(widgets->find(WUID_IMPORT_MENU));
            if (menu != NULL)
            {
                // Hydrogen drumkit import
                tk::MenuItem *child = new tk::MenuItem(pDisplay);
                widgets->add(child);
                child->init();
                child->text()->set("actions.import_sfz_file");
                child->slots()->bind(tk::SLOT_SUBMIT, slot_start_import_sfz_file, this);
                menu->add(child);

                // SFZ import
                child = new tk::MenuItem(pDisplay);
                widgets->add(child);
                child->init();
                child->text()->set("actions.import_hydrogen_drumkit_file");
                child->slots()->bind(tk::SLOT_SUBMIT, slot_start_import_hydrogen_file, this);
                menu->add(child);

                // Bundle import
                child = new tk::MenuItem(pDisplay);
                widgets->add(child);
                child->init();
                child->text()->set("actions.sampler.import_bundle");
                child->slots()->bind(tk::SLOT_SUBMIT, slot_start_import_sampler_bundle, this);
                menu->add(child);
            }

            // Synchronize list of Hydrogen files
            sync_hydrogen_files();

            // Add more menu items
            menu                    = tk::widget_cast<tk::Menu>(widgets->find(WUID_EXPORT_MENU));
            if (menu != NULL)
            {
                tk::MenuItem *child = new tk::MenuItem(pDisplay);
                widgets->add(child);
                child->init();
                child->text()->set("actions.sampler.export_bundle");
                child->slots()->bind(tk::SLOT_SUBMIT, slot_start_export_sampler_bundle, this);
                menu->add(child);
            }

            // Find port names
            char name[0x40];
            for (size_t i=0; i < meta::sampler_metadata::INSTRUMENTS_MAX; ++i)
            {
                // Verify that the corresponding instrument exists
                snprintf(name, sizeof(name), "chan_%d", int(i));
                ui::IPort *port = wrapper()->port(name);
                if (port == NULL)
                    continue;

                // Obtain the widget and bind slot
                snprintf(name, sizeof(name), "iname_%d", int(i));
                tk::Edit *ed = wrapper()->controller()->widgets()->get<tk::Edit>(name);
                if (ed == NULL)
                    continue;
                ed->slots()->bind(tk::SLOT_CHANGE, slot_instrument_name_updated, this);

                // Append widget binding
                inst_name_t *inst = vInstNames.add();
                if (inst == NULL)
                    return STATUS_NO_MEM;

                inst->wEdit     = ed;
                inst->wListItem = (wInstrumentsGroup != NULL) ? wInstrumentsGroup->items()->get(i) : NULL;
                inst->nIndex    = i;
                inst->bChanged  = false;
            }

            // Drag&Drop
            pDragInSink = new DragInSink(this);
            if (pDragInSink == NULL)
                return STATUS_NO_MEM;
            pDragInSink->acquire();

            pWrapper->window()->slots()->bind(tk::SLOT_DRAG_REQUEST, slot_drag_request, this);

            return STATUS_OK;
        }

        void sampler_ui::destroy_hydrogen_menus()
        {
            // Destroy all information about drumkits
            for (size_t i=0, n=vDrumkits.size(); i<n; ++i)
            {
                h2drumkit_t *dk = vDrumkits.uget(i);
                if (dk == NULL)
                    continue;
                dk->pMenu       = NULL;
                delete dk;
            }
            vDrumkits.flush();

            // Destroy all menu widgets
            for (size_t i=0, n=vHydrogenMenus.size(); i<n; ++i)
            {
                tk::Widget *w = vHydrogenMenus.uget(i);
                if (w == NULL)
                    continue;
                w->destroy();
                delete w;
            }
            vHydrogenMenus.flush();
        }

        void sampler_ui::sync_hydrogen_files()
        {
            // Destroy previous data and lookup for hydrogen drumkits
            destroy_hydrogen_menus();
            lookup_hydrogen_files();
            if (vDrumkits.is_empty())
                return;

            tk::Registry *widgets   = pWrapper->controller()->widgets();
            tk::Menu *menu          = tk::widget_cast<tk::Menu>(widgets->find(WUID_IMPORT_MENU));
            if (menu == NULL)
                return;

            // Create menu item
            tk::MenuItem *child     = new tk::MenuItem(pDisplay);
            vHydrogenMenus.add(child);
            child->init();
            child->text()->set("actions.import_installed_hydrogen_drumkit");
            menu->add(child);

            // Create submenu
            menu            = new tk::Menu(pDisplay);
            vHydrogenMenus.add(menu);
            menu->init();
            child->menu()->set(menu);

            // Add hydrogen files to menu
            LSPString tmp;

            for (size_t i=0, n=vDrumkits.size(); i<n; ++i)
            {
                h2drumkit_t *h2 = vDrumkits.uget(i);

                child   = new tk::MenuItem(pDisplay);
                vHydrogenMenus.add(child);
                child->init();
                child->text()->set(
                    (h2->enType == H2DRUMKIT_SYSTEM) ? "labels.file_display.system" :
                    (h2->enType == H2DRUMKIT_USER) ? "labels.file_display.user" :
                    "labels.file_display.custom");
                child->text()->params()->set_string("file", h2->sPath.as_string());
                if (h2->sPath.get_parent(&tmp) == STATUS_OK)
                    child->text()->params()->set_string("parent", &tmp);
                if (h2->sPath.get_last(&tmp) == STATUS_OK)
                    child->text()->params()->set_string("name", &tmp);
                child->text()->params()->set_string("title", &h2->sName);

                // Bind
                child->slots()->bind(tk::SLOT_SUBMIT, slot_import_hydrogen_file, this);
                menu->add(child);
                h2->pMenu       = child;
            }
        }

        void sampler_ui::idle()
        {
            // Single instrument sample does not require to do anything
            if (!bMultiple)
                return;

            // Scan the list of instrument names for changes
            size_t changes = 0;
            for (size_t i=0, n=vInstNames.size(); i<n; ++i)
            {
                inst_name_t *name = vInstNames.uget(i);
                if ((name->wEdit) && (name->bChanged))
                    ++changes;
            }

            // Apply instrument names to KVT
            if (changes > 0)
            {
                core::KVTStorage *kvt = wrapper()->kvt_lock();
                if (kvt != NULL)
                {
                    lsp_finally { wrapper()->kvt_release(); };

                    LSPString name;
                    for (size_t i=0, n=vInstNames.size(); i<n; ++i)
                    {
                        inst_name_t *inst = vInstNames.uget(i);
                        if ((!inst->wEdit) || (!inst->bChanged))
                            continue;

                        // Obtain the new instrument name
                        if (inst->wEdit->text()->format(&name) != STATUS_OK)
                            continue;

                        // Submit new value to KVT
                        set_kvt_instrument_name(kvt, inst->nIndex, name.get_utf8());
                    }
                }
            }
        }

        status_t sampler_ui::reset_settings()
        {
            // Single instrument sample does not require to do anything
            if (!bMultiple)
                return STATUS_OK;

            core::KVTStorage *kvt = wrapper()->kvt_lock();
            if (kvt != NULL)
            {
                lsp_finally { wrapper()->kvt_release(); };

                // Reset all names for all instruments
                for (size_t i=0, n=vInstNames.size(); i<n; ++i)
                {
                    inst_name_t *name = vInstNames.uget(i);
                    if (!name->wEdit)
                        continue;
                    set_kvt_instrument_name(kvt, name->nIndex, "");
                    name->bChanged  = false;
                }
            }

            return STATUS_OK;
        }

        void sampler_ui::init_path(tk::Widget *sender, ui::IPort *path, ui::IPort *file_type)
        {
            tk::FileDialog *dlg = tk::widget_cast<tk::FileDialog>(sender);
            if (dlg == NULL)
                return;

            if (path != NULL)
                dlg->path()->set_raw(path->buffer<char>());
            if (file_type != NULL)
                dlg->selected_filter()->set(size_t(file_type->value()));
        }

        void sampler_ui::commit_path(tk::Widget *sender, ui::IPort *path, ui::IPort *file_type)
        {
            tk::FileDialog *dlg = tk::widget_cast<tk::FileDialog>(sender);
            if (dlg == NULL)
                return;

            if (path != NULL)
            {
                LSPString fpath;
                if ((dlg->path()->format(&fpath) == STATUS_OK))
                {
                    const char *upath = fpath.get_utf8();
                    path->write(upath, ::strlen(upath));
                    path->notify_all(ui::PORT_USER_EDIT);
                }
            }
            if (file_type != NULL)
            {
                LSPString fpath;
                file_type->set_value(dlg->selected_filter()->get());
                file_type->notify_all(ui::PORT_USER_EDIT);
            }
        }

        void sampler_ui::kvt_changed(core::KVTStorage *kvt, const char *id, const core::kvt_param_t *value)
        {
            // Single instrument sample does not require to do anything
            if (!bMultiple)
                return;

            if ((value->type == core::KVT_STRING) && (::strstr(id, "/instrument/") == id))
            {
                id += ::strlen("/instrument/");

                char *endptr = NULL;
                errno = 0;
                long index = ::strtol(id, &endptr, 10);

                // Valid object number?
                if ((errno == 0) && (!::strcmp(endptr, "/name")) && (index >= 0))
                {
                    LSPString name;
                    name.set_utf8(value->str);

                    for (size_t i=0, n=vInstNames.size(); i<n; ++i)
                    {
                        inst_name_t *inst = vInstNames.uget(i);
                        if ((!inst->wEdit) || (inst->nIndex != size_t(index)))
                            continue;

                        set_ui_instrument_name(inst, &name);
                        inst->bChanged = false;
                    }
                }
            }
        }

        ssize_t sampler_ui::cmp_drumkit_files(const h2drumkit_t *a, const h2drumkit_t *b)
        {
            return a->sName.compare_to_nocase(&b->sName);
        }

        status_t sampler_ui::add_drumkit(const io::Path *base, const io::Path *path, const hydrogen::drumkit_t *dk, h2drumkit_type_t type)
        {
            h2drumkit_t *drumkit = new h2drumkit_t();
            if (drumkit == NULL)
                return STATUS_NO_MEM;
            lsp_finally {
                if (drumkit != NULL)
                    delete drumkit;
            };

            if (!drumkit->sName.set(&dk->name))
                return STATUS_NO_MEM;
            if (drumkit->sBase.set(base) != STATUS_OK)
                return STATUS_NO_MEM;
            if (drumkit->sPath.set(path) != STATUS_OK)
                return STATUS_NO_MEM;

            drumkit->enType         = type;
            drumkit->pMenu          = NULL;

            if (vDrumkits.add(drumkit))
            {
                drumkit = NULL;
                return STATUS_OK;
            }

            return STATUS_NO_MEM;
        }

        status_t sampler_ui::scan_hydrogen_directory(const io::Path *path, h2drumkit_type_t type)
        {
            status_t res;
            io::Path dir, subdir;
            io::fattr_t fa;

            // Open the directory
            if ((res = dir.set(path)) != STATUS_OK)
                return res;
            if (type != H2DRUMKIT_CUSTOM)
            {
                if ((res = dir.append_child("data" FILE_SEPARATOR_S "drumkits")) != STATUS_OK)
                    return res;
            }

            io::Dir fd;
            if ((res = fd.open(&dir)) != STATUS_OK)
                return res;

            // Read the directory
            while ((res = fd.read(&subdir, true)) == STATUS_OK)
            {
                if ((subdir.is_dot()) || (subdir.is_dotdot()))
                    continue;

                // Find all subdirectories
                if ((res = io::File::sym_stat(&subdir, &fa)) != STATUS_OK)
                    continue;
                if (fa.type != io::fattr_t::FT_DIRECTORY)
                    continue;

                // Lookup for drumkit file
                if ((res = subdir.append_child("drumkit.xml")) != STATUS_OK)
                    continue;

                // Load drumkit settings
                hydrogen::drumkit_t dk;
                if ((res = hydrogen::load(&subdir, &dk)) != STATUS_OK)
                    continue;

                // If all is OK, add drumkit metadata
                if ((res = add_drumkit(&dir, &subdir, &dk, type)) != STATUS_OK)
                {
                    fd.close();
                    return STATUS_NO_MEM;
                }
            }

            // Close the directory
            fd.close();

            return (res == STATUS_EOF) ? STATUS_OK : res;
        }

        void sampler_ui::lookup_hydrogen_files()
        {
            // Lookup in system directories
            status_t res;
            io::Path dir, subdir;
            for (const char **path = h2_system_paths; (path != NULL) && (*path != NULL); ++path)
            {
                if (dir.set(*path) != STATUS_OK)
                    continue;

                scan_hydrogen_directory(&dir, H2DRUMKIT_SYSTEM);
            }

            // Lookup in user's home directory
            if (system::get_home_directory(&dir) == STATUS_OK)
            {
                for (const char **path = h2_user_paths; (path != NULL) && (*path != NULL); ++path)
                {
                    if (subdir.set(&dir) != STATUS_OK)
                        continue;
                    if (subdir.append_child(*path) != STATUS_OK)
                        continue;

                    scan_hydrogen_directory(&subdir, H2DRUMKIT_USER);
                }
            }

            // Lookup custom user path
            if ((res = read_path(&dir, UI_USER_HYDROGEN_KIT_PATH_PORT)) == STATUS_OK)
                scan_hydrogen_directory(&dir, H2DRUMKIT_CUSTOM);

            // Sort the result
            if (vDrumkits.size() >= 2)
                vDrumkits.qsort(cmp_drumkit_files);
        }

        status_t sampler_ui::slot_import_hydrogen_file(tk::Widget *sender, void *ptr, void *data)
        {
            sampler_ui *_this = static_cast<sampler_ui *>(ptr);
            if (_this == NULL)
                return STATUS_BAD_STATE;

            for (size_t i=0, n=_this->vDrumkits.size(); i<n; ++i)
            {
                h2drumkit_t *h2 = _this->vDrumkits.uget(i);
                if (h2->pMenu == sender)
                {
                    _this->import_drumkit_file(&h2->sBase, h2->sPath.as_string());
                    break;
                }
            }

            return STATUS_OK;
        }

        status_t sampler_ui::slot_start_import_hydrogen_file(tk::Widget *sender, void *ptr, void *data)
        {
            sampler_ui *_this = static_cast<sampler_ui *>(ptr);

            tk::FileDialog *dlg = _this->wHydrogenImport;
            if (dlg == NULL)
            {
                dlg = new tk::FileDialog(_this->pDisplay);
                _this->pWrapper->controller()->widgets()->add(dlg);
                _this->wHydrogenImport  = dlg;

                dlg->init();
                dlg->mode()->set(tk::FDM_OPEN_FILE);
                dlg->title()->set("titles.import_hydrogen_drumkit");
                dlg->action_text()->set("actions.import");

                tk::FileFilters *f = dlg->filter();
                {
                    tk::FileMask *ffi;

                    if ((ffi = f->add()) != NULL)
                    {
                        ffi->pattern()->set("*.xml");
                        ffi->title()->set("files.hydrogen.xml");
                        ffi->extensions()->set_raw("");
                    }

                    if ((ffi = f->add()) != NULL)
                    {
                        ffi->pattern()->set("*");
                        ffi->title()->set("files.all");
                        ffi->extensions()->set_raw("");
                    }
                }

                dlg->slots()->bind(tk::SLOT_SUBMIT, slot_call_import_hydrogen_file, _this);
                dlg->slots()->bind(tk::SLOT_SHOW, slot_fetch_hydrogen_path, _this);
                dlg->slots()->bind(tk::SLOT_HIDE, slot_commit_hydrogen_path, _this);
            }

            dlg->show(_this->pWrapper->window());
            return STATUS_OK;
        }

        status_t sampler_ui::slot_call_import_hydrogen_file(tk::Widget *sender, void *ptr, void *data)
        {
            sampler_ui *_this = static_cast<sampler_ui *>(ptr);
            LSPString path;
            status_t res = _this->wHydrogenImport->selected_file()->format(&path);
            if (res == STATUS_OK)
                res = _this->import_drumkit_file(NULL, &path);
            return STATUS_OK;
        }

        status_t sampler_ui::slot_fetch_hydrogen_path(tk::Widget *sender, void *ptr, void *data)
        {
            sampler_ui *_this = static_cast<sampler_ui *>(ptr);
            if (_this == NULL)
                return STATUS_BAD_STATE;

            _this->init_path(sender, _this->pHydrogenPath, _this->pHydrogenFileType);
            return STATUS_OK;
        }

        status_t sampler_ui::slot_commit_hydrogen_path(tk::Widget *sender, void *ptr, void *data)
        {
            sampler_ui *_this = static_cast<sampler_ui *>(ptr);
            if (_this == NULL)
                return STATUS_BAD_STATE;
            _this->commit_path(sender, _this->pHydrogenPath, _this->pHydrogenFileType);

            return STATUS_OK;
        }

        status_t sampler_ui::slot_instrument_name_updated(tk::Widget *sender, void *ptr, void *data)
        {
            sampler_ui *self = static_cast<sampler_ui *>(ptr);

            // Pass the changed value to the corresponding widget
            ssize_t selected = ((self->pCurrentInstrument != NULL)) ? ssize_t(self->pCurrentInstrument->value()) : -1;

            if ((sender != NULL) && (sender == self->wCurrentInstrument))
            {
                // Mark the corresponding instrument name being changed
                for (size_t i=0, n=self->vInstNames.size(); i<n; ++i)
                {
                    inst_name_t *inst = self->vInstNames.uget(i);
                    if ((ssize_t(inst->nIndex) == selected) && (inst->wEdit != NULL))
                    {
                        LSPString name;
                        self->wCurrentInstrument->text()->format(&name);
                        self->set_ui_instrument_name(inst, &name);

                        inst->bChanged  = true;
                    }
                }
            }
            else
            {
                // Mark the corresponding instrument name being changed
                for (size_t i=0, n=self->vInstNames.size(); i<n; ++i)
                {
                    inst_name_t *inst = self->vInstNames.uget(i);
                    if (inst->wEdit != sender)
                        continue;

                    LSPString name;
                    inst->wEdit->text()->format(&name);
                    self->set_ui_instrument_name(inst, &name);
                    inst->bChanged  = true;
                }
            }

            return STATUS_OK;
        }

        void sampler_ui::set_float_value(float value, const char *fmt...)
        {
            char port_id[32];
            va_list v;
            va_start(v, fmt);

            ::vsnprintf(port_id, sizeof(port_id)/sizeof(char), fmt, v);
            ui::IPort *p = pWrapper->port(port_id);
            if (p != NULL)
            {
                p->set_value(value);
                p->notify_all(ui::PORT_USER_EDIT);
            }

            va_end(v);
        }

        void sampler_ui::set_path_value(const char *path, const char *fmt...)
        {
            char port_id[32];
            va_list v;
            va_start(v, fmt);

            ::vsnprintf(port_id, sizeof(port_id)/sizeof(char), fmt, v);
            ui::IPort *p = pWrapper->port(port_id);
            if ((p != NULL) && (meta::is_path_port(p->metadata())))
            {
                p->write(path, strlen(path));
                p->notify_all(ui::PORT_USER_EDIT);
            }

            va_end(v);
        }

        void sampler_ui::set_kvt_instrument_name(core::KVTStorage *kvt, int id, const char *name)
        {
            char kvt_name[0x80];
            core::kvt_param_t kparam;

            // Submit new value to KVT
            snprintf(kvt_name, sizeof(kvt_name), "/instrument/%d/name", id);
            kparam.type     = core::KVT_STRING;
            kparam.str      = name;
            lsp_trace("%s = %s", kvt_name, kparam.str);
            kvt->put(kvt_name, &kparam, core::KVT_RX);
            wrapper()->kvt_notify_write(kvt, kvt_name, &kparam);
        }

        status_t sampler_ui::import_hydrogen_file(const LSPString *path)
        {
            lsp_trace("Importing Hydrogen file from %s", path->get_utf8());

            // Load settings
            hydrogen::drumkit_t dk;
            status_t res = hydrogen::load(path, &dk);
            if (res != STATUS_OK)
                return res;

            // Get
            io::Path base, fpath;
            if ((res = base.set(path)) != STATUS_OK)
                return res;
            if ((res = base.remove_last()) != STATUS_OK)
                return res;

            // Reset settings to default
            if ((res = pWrapper->reset_settings()) != STATUS_OK)
                return res;

            for (size_t i=0, id=0; i < meta::sampler_metadata::INSTRUMENTS_MAX; ++i)
            {
                hydrogen::instrument_t *inst = dk.instruments.get(i);
                size_t jd = 0;

                if (inst != NULL)
                {
                    if (inst->layers.size() > 0)
                    {
                        for (size_t j=0, m=inst->layers.size(); j<m; ++j)
                        {
                            hydrogen::layer_t *layer = inst->layers.get(j);
                            if (layer->file_name.is_empty())
                                continue;
                            if ((res = add_sample(&base, id, jd, layer)) != STATUS_OK)
                                return res;

                            ++jd; // Increment sample number
                        }
                    }
                    else if (!inst->file_name.is_empty())
                    {
                        hydrogen::layer_t layer;
                        layer.min   = 0.0f;
                        layer.max   = 1.0f;
                        layer.gain  = inst->gain;
                        layer.pitch = 0.0f;
                        layer.file_name.set(&inst->file_name);

                        if ((res = add_sample(&base, id, jd, &layer)) != STATUS_OK)
                            return res;
                        ++jd; // Increment sample number
                    }
                }

                // Reset non-used samples
                for (; jd < meta::sampler_metadata::SAMPLE_FILES; ++jd)
                {
                    if ((res = add_sample(&base, id, jd, NULL)) != STATUS_OK)
                        return res;
                }

                // Add instrument
                if ((res = add_instrument(id, inst)) != STATUS_OK)
                    return res;
                ++id;
            }

            return STATUS_OK;
        }

        status_t sampler_ui::read_path(io::Path *dst, const char *port_id)
        {
            // Try to get path
            ui::IPort *port = pWrapper->port(port_id);
            if ((port == NULL) || (!meta::is_path_port(port->metadata())))
                return STATUS_NOT_FOUND;

            const char *path = port->buffer<const char>();
            if ((path == NULL) || (strlen(path) <= 0))
                return STATUS_NOT_FOUND;

            // Try to parse path as an expression with environment variables
            status_t res = STATUS_OK;
            expr::Expression expr;

            if (expr.parse(path, expr::Expression::FLAG_STRING) != STATUS_OK)
                return dst->set(path);

            expr::EnvResolver resolver;
            expr::value_t v;

            expr.set_resolver(&resolver);
            expr::init_value(&v);
            lsp_finally { expr::destroy_value(&v); };

            res = expr.evaluate(&v);
            if (res == STATUS_OK)
                res = expr::cast_string(&v);

            return (res == STATUS_OK) ? dst->set(v.v_str) : dst->set(path);
        }

        status_t sampler_ui::import_drumkit_file(const io::Path *base, const LSPString *path)
        {
            status_t res;
            io::Path file, config, kit_path, override_kit_path;
            LSPString file_ext;

            // Check that hydrogen override it turned on
            ui::IPort *flag = pWrapper->port(UI_OVERRIDE_HYDROGEN_KITS_PORT);
            if ((flag == NULL) || (!meta::is_control_port(flag->metadata())))
                return import_hydrogen_file(path);
            if (flag->value() <= 0.5f)
                return import_hydrogen_file(path);

            // Check extension and skip override if configuration file is used
            if ((res = file.set(path)) != STATUS_OK)
                return res;
            if (file.get_ext(&file_ext) != STATUS_OK)
                return import_hydrogen_file(path);
            if (file_ext.equals_ascii_nocase("cfg"))
                return pWrapper->import_settings(path, ui::IMPORT_FLAG_NONE);

            // Replace file extension with '.cfg'
            if ((res = file.get_noext(&config)) != STATUS_OK)
                return res;
            if ((res = config.append(".cfg")) != STATUS_OK)
                return res;

            // Remove base directory, exit if it is not possible
            bool removed = false;
            read_path(&kit_path, UI_USER_HYDROGEN_KIT_PATH_PORT);
            read_path(&override_kit_path, UI_OVERRIDE_HYDROGEN_KIT_PATH_PORT);

            if ((base != NULL) && (config.remove_base(base) == STATUS_OK))
                removed = true;
            if ((!removed) && (!kit_path.is_empty()))
            {
                if (config.remove_base(&kit_path) == STATUS_OK)
                    removed = true;
            }
            if ((!removed) && (!override_kit_path.is_empty()))
            {
                if (config.remove_base(&override_kit_path) == STATUS_OK)
                    removed = true;
            }
            if (!removed)
                return import_hydrogen_file(path);

            // Try to load files from overridden paths
            if ((res = try_override_hydrogen_file(&override_kit_path, &config)) == STATUS_OK)
                return res;
            if ((res = try_override_hydrogen_file(&kit_path, &config)) == STATUS_OK)
                return res;
            return import_hydrogen_file(path);
        }

        status_t sampler_ui::try_override_hydrogen_file(const io::Path *base, const io::Path *relative)
        {
            io::Path file;
            status_t res;

            if (base->is_empty())
                return STATUS_NOT_FOUND;
            if ((res = file.set(base, relative)) != STATUS_OK)
                return res;
            if (!file.is_reg())
                return STATUS_NOT_FOUND;

            lsp_trace("Overriding Hydrogen file from %s...", file.as_utf8());
            if ((res = pWrapper->import_settings(&file, ui::IMPORT_FLAG_NONE)) != STATUS_OK)
            {
                lsp_trace("Failed to override Hydrogen file from %s: error %d", file.as_utf8(), int(res));
                return res;
            }

            return STATUS_OK;
        }

        status_t sampler_ui::add_sample(const io::Path *base, int id, int jd, const hydrogen::layer_t *layer)
        {
            io::Path path;
            status_t res;

            if (layer != NULL)
            {
                if ((res = path.set(base)) != STATUS_OK)
                    return res;
                if ((res = path.append_child(&layer->file_name)) != STATUS_OK)
                    return res;

                set_path_value(path.as_native(), "sf_%d_%d", id, jd);       // sample file
                set_float_value(layer->gain, "mk_%d_%d", id, jd);           // makeup gain
                set_float_value(layer->max * 100.0f, "vl_%d_%d", id, jd);   // velocity
                set_float_value(layer->pitch, "pi_%d_%d", id, jd);          // pitch
            }
            else
            {
                set_path_value("", "sf_%d_%d", id, jd);                     // sample file
                set_float_value(GAIN_AMP_0_DB, "mk_%d_%d", id, jd);         // makeup gain
                set_float_value((100.0f * (8 - jd)) / meta::sampler_metadata::SAMPLE_FILES, "vl_%d_%d", id, jd);    // velocity
                set_float_value(0.0f, "pi_%d_%d", id, jd);                  // pitch
            }

            return STATUS_OK;
        }

        status_t sampler_ui::add_instrument(int id, const hydrogen::instrument_t *inst)
        {
            // Reset to defaults
            if (inst != NULL)
            {
                set_float_value(inst->volume, "imix_%d", id);                           // instrument mix gain

                // MIDI channel
                int channel = (inst->midi_out_channel >= 0) ? inst->midi_out_channel : inst->midi_in_channel;
                if (channel >= 0)
                    set_float_value(channel, "chan_%d", id);    // channel

                // Midi Note/octave
                int note = (inst->midi_out_note >= 0) ? inst->midi_out_note : inst->midi_in_note;
                if (note >= 0)
                {
                    int octave       = note / 12;
                    note            %= 12;

                    set_float_value(note, "note_%d", id);       // note
                    set_float_value(octave, "oct_%d", id);      // octave
                }

                if (inst->mute_group >= 0)
                    set_float_value(inst->mute_group + 1, "mgrp_%d", id);               // mute group

                set_float_value((inst->stop_note) ? 1.0f : 0.0f, "nto_%d", id);         // note off handling
                set_float_value(200.0f * (0.5f - inst->pan_left), "panl_%d", id);       // instrument pan left
                set_float_value(200.0f * (inst->pan_right - 0.5f), "panr_%d", id);      // instrument pan right
            }
            else
            {
                set_float_value(GAIN_AMP_0_DB, "imix_%d", id);                          // instrument mix gain
                set_float_value(0.0f, "nto_%d", id);                                    // note off handling
                set_float_value(-100.0f, "panl_%d", id);                                // instrument pan left
                set_float_value(+100.0f, "panr_%d", id);                                // instrument pan right
            }

            // Set instrument name
            core::KVTStorage *kvt = wrapper()->kvt_lock();
            if (kvt != NULL)
            {
                lsp_finally { wrapper()->kvt_release(); };
                set_kvt_instrument_name(kvt, id, (inst != NULL) ? inst->name.get_utf8() : "");
            }

            return STATUS_OK;
        }

        void sampler_ui::notify(ui::IPort *port, size_t flags)
        {
            if (port == NULL)
                return;

            if (port == pCurrentInstrument)
            {
                core::KVTStorage *kvt = wrapper()->kvt_lock();
                if (kvt != NULL)
                {
                    lsp_finally { wrapper()->kvt_release(); };

                    const char *param = "";
                    char buf[0x40];
                    snprintf(buf, sizeof(buf), "/instrument/%d/name", int(pCurrentInstrument->value()));
                    if (kvt->get(buf, &param) != STATUS_OK)
                        param = "";
                    wCurrentInstrument->text()->set_raw(param);
                }
            }

            if (port == pHydrogenCustomPath)
                sync_hydrogen_files();
        }

        tk::FileDialog *sampler_ui::get_bundle_dialog(bool import)
        {
            tk::FileDialog *dlg = wBundleDialog;
            if (dlg == NULL)
            {
                dlg             = new tk::FileDialog(pDisplay);
                wBundleDialog   = dlg;
                pWrapper->controller()->widgets()->add(dlg);

                dlg->init();

                tk::FileFilters *f = dlg->filter();
                {
                    tk::FileMask *ffi;

                    if ((ffi = f->add()) != NULL)
                    {
                        ffi->pattern()->set("*.lspc");
                        ffi->title()->set("files.sampler.lspc");
                        ffi->extensions()->set_raw(".lspc");
                    }

                    if ((ffi = f->add()) != NULL)
                    {
                        ffi->pattern()->set("*");
                        ffi->title()->set("files.all");
                        ffi->extensions()->set_raw("");
                    }
                }

                dlg->slots()->bind(tk::SLOT_SUBMIT, slot_call_process_sampler_bundle, this);
                dlg->slots()->bind(tk::SLOT_SHOW, slot_fetch_sampler_bundle_path, this);
                dlg->slots()->bind(tk::SLOT_HIDE, slot_commit_sampler_bundle_path, this);
            }

            if (import)
            {
                dlg->mode()->set(tk::FDM_OPEN_FILE);
                dlg->title()->set("titles.sampler.import_bundle");
                dlg->action_text()->set("actions.import");
            }
            else
            {
                dlg->mode()->set(tk::FDM_SAVE_FILE);
                dlg->title()->set("titles.sampler.export_bundle");
                dlg->action_text()->set("actions.export");
            }

            return wBundleDialog;
        }

        status_t sampler_ui::slot_start_export_sampler_bundle(tk::Widget *sender, void *ptr, void *data)
        {
            sampler_ui *_this = static_cast<sampler_ui *>(ptr);
            tk::FileDialog *dlg = _this->get_bundle_dialog(false);
            if (dlg != NULL)
                dlg->show(_this->pWrapper->window());
            return STATUS_OK;
        }

        status_t sampler_ui::slot_start_import_sampler_bundle(tk::Widget *sender, void *ptr, void *data)
        {
            sampler_ui *_this = static_cast<sampler_ui *>(ptr);
            tk::FileDialog *dlg = _this->get_bundle_dialog(true);
            if (dlg != NULL)
                dlg->show(_this->pWrapper->window());
            return STATUS_OK;
        }

        status_t sampler_ui::slot_fetch_sampler_bundle_path(tk::Widget *sender, void *ptr, void *data)
        {
            sampler_ui *_this = static_cast<sampler_ui *>(ptr);
            if (_this == NULL)
                return STATUS_BAD_STATE;

            _this->init_path(sender, _this->pBundlePath, _this->pBundleFileType);
            return STATUS_OK;
        }

        status_t sampler_ui::slot_commit_sampler_bundle_path(tk::Widget *sender, void *ptr, void *data)
        {
            sampler_ui *_this = static_cast<sampler_ui *>(ptr);
            if (_this == NULL)
                return STATUS_BAD_STATE;

            _this->commit_path(sender, _this->pBundlePath, _this->pBundleFileType);
            return STATUS_OK;
        }

        void sampler_ui::show_message(const char *title, const char *message, const expr::Parameters *params)
        {
            tk::MessageBox *dlg = wMessageBox;
            if (dlg == NULL)
            {
                dlg             = new tk::MessageBox(pDisplay);
                wMessageBox     = dlg;
                pWrapper->controller()->widgets()->add(dlg);

                dlg->init();
                dlg->add("actions.ok", slot_close_message_box, dlg);
            }

            dlg->title()->set(title);
            dlg->message()->set(message, params);
            dlg->show(pWrapper->window());
        }

        status_t sampler_ui::slot_close_message_box(tk::Widget *sender, void *ptr, void *data)
        {
            tk::MessageBox *dlg = tk::widget_ptrcast<tk::MessageBox>(data);
            if (dlg != NULL)
                dlg->hide();
            return STATUS_OK;
        }

        status_t sampler_ui::allocate_temp_file(io::Path *dst, const io::Path *src)
        {
            const char *spath = src->as_utf8();
            for (int i=0; ; ++i)
            {
                if (dst->fmt("%s.%d", spath, i) <= 0)
                    return STATUS_NO_MEM;
                if (!dst->exists())
                    break;
            }

            return STATUS_OK;
        }

        status_t sampler_ui::slot_call_process_sampler_bundle(tk::Widget *sender, void *ptr, void *data)
        {
            sampler_ui *_this = static_cast<sampler_ui *>(ptr);
            if (_this == NULL)
                return STATUS_BAD_STATE;

            LSPString path;
            status_t res = _this->wBundleDialog->selected_file()->format(&path);
            if (res == STATUS_OK)
            {
                if (_this->wBundleDialog->mode()->get() == tk::FDM_SAVE_FILE)
                {
                    // Export bundle data
                    io::Path dst, temp;
                    res = dst.set(&path);
                    if (res == STATUS_OK)
                        res = allocate_temp_file(&temp, &dst);
                    if (res == STATUS_OK)
                        res = _this->export_sampler_bundle(&temp);
                    if (res == STATUS_OK)
                    {
                        dst.remove();
                        res = temp.rename(&dst);
                    }
                }
                else
                {
                    // Import bundle data
                    io::Path src;
                    res = src.set(&path);
                    if (res == STATUS_OK)
                        res = _this->import_sampler_bundle(&src);
                }

                // Analyze result and output message if there were errors
                if (res != STATUS_OK)
                {
                    expr::Parameters params;
                    tk::prop::String str;
                    LSPString status_key;
                    status_key.append_ascii("statuses.std.");
                    status_key.append_ascii(lsp::get_status_lc_key(res));
                    str.bind(_this->wBundleDialog->style(), _this->pDisplay->dictionary());
                    str.set(&status_key);

                    params.set_string("reason", str.formatted());
                    _this->show_message("titles.sampler.warning", "messages.sampler.failed_to_process_bundle", &params);
                }
            }
            return STATUS_OK;
        }

        status_t sampler_ui::export_sampler_bundle(const io::Path *path)
        {
            status_t res;

            // Obtain the parent directory
            io::Path basedir;
            io::Path *base = (path->get_parent(&basedir) == STATUS_OK) ? &basedir : NULL;

            // Create LSPC file
            lspc::File fd;
            if ((res = fd.create(path)) != STATUS_OK)
                return res;

            // Write configuration chunk to the file
            io::IOutStream *os = NULL;
            if ((res = lspc::write_config(NULL, &fd, &os)) != STATUS_OK)
            {
                fd.close();
                return res;
            }

            // Create bundle serializer
            BundleSerializer s(this, &fd);
            if ((res = s.wrap(os, WRAP_CLOSE | WRAP_DELETE, "UTF-8")) != STATUS_OK)
            {
                os->close();
                delete os;
                fd.close();
                return res;
            }

            // Call wrapper to serialize
            if ((res = pWrapper->export_settings(&s, base)) != STATUS_OK)
            {
                s.close();
                fd.close();
                return res;
            }

            // Close the bundle serializer
            if ((res = s.close()) != STATUS_OK)
            {
                fd.close();
                return res;
            }

            // Close the LSPC file and return
            return fd.close();
        }

        status_t sampler_ui::import_sampler_bundle(const io::Path *path)
        {
            status_t res;

            // Obtain the parent directory
            io::Path basedir;
            io::Path *base = (path->get_parent(&basedir) == STATUS_OK) ? &basedir : NULL;

            // Open LSPC file
            lspc::File fd;
            if ((res = fd.open(path)) != STATUS_OK)
                return res;

            // Find LSPC chunk
            lspc::chunk_id_t *chunk_ids = NULL;
            ssize_t nchunks     = fd.enumerate_chunks(LSPC_CHUNK_TEXT_CONFIG, &chunk_ids);
            if (nchunks <= 0)
            {
                fd.close();
                return (nchunks < 0) ? -nchunks : STATUS_NOT_FOUND;
            }
            lsp_finally { free(chunk_ids); };

            // Obtain the input stream from the LSPC
            io::IInStream *is = NULL;
            if ((res = lspc::read_config(chunk_ids[0], &fd, &is)) != STATUS_OK)
            {
                fd.close();
                return res;
            }

            // Create deserializer
            BundleDeserializer d(this, path);
            if ((res = d.wrap(is, WRAP_CLOSE | WRAP_DELETE, "UTF-8")) != STATUS_OK)
            {
                is->close();
                delete is;
                fd.close();
                return res;
            }

            // Call wrapper to deserialize
            if ((res = pWrapper->import_settings(&d, ui::IMPORT_FLAG_PRESET, base)) != STATUS_OK)
            {
                d.close();
                fd.close();
                return res;
            }

            // Close the bundle serializer
            if ((res = d.close()) != STATUS_OK)
            {
                fd.close();
                return res;
            }

            // Close the LSPC file and return
            return fd.close();
        }

        status_t sampler_ui::slot_start_import_sfz_file(tk::Widget *sender, void *ptr, void *data)
        {
            sampler_ui *_this = static_cast<sampler_ui *>(ptr);

            tk::FileDialog *dlg = _this->wSfzImport;
            if (dlg == NULL)
            {
                dlg = new tk::FileDialog(_this->pDisplay);
                _this->pWrapper->controller()->widgets()->add(dlg);
                _this->wSfzImport   = dlg;

                dlg->init();
                dlg->mode()->set(tk::FDM_OPEN_FILE);
                dlg->title()->set("titles.import_sfz");
                dlg->action_text()->set("actions.import");

                tk::FileFilters *f = dlg->filter();
                {
                    tk::FileMask *ffi;

                    if ((ffi = f->add()) != NULL)
                    {
                        ffi->pattern()->set("*.sfz");
                        ffi->title()->set("files.sfz");
                        ffi->extensions()->set_raw("");
                    }

                    if ((ffi = f->add()) != NULL)
                    {
                        ffi->pattern()->set("*");
                        ffi->title()->set("files.all");
                        ffi->extensions()->set_raw("");
                    }
                }

                dlg->slots()->bind(tk::SLOT_SUBMIT, slot_call_import_sfz_file, _this);
                dlg->slots()->bind(tk::SLOT_SHOW, slot_fetch_sfz_path, _this);
                dlg->slots()->bind(tk::SLOT_HIDE, slot_commit_sfz_path, _this);
            }

            dlg->show(_this->pWrapper->window());
            return STATUS_OK;
        }

        status_t sampler_ui::slot_call_import_sfz_file(tk::Widget *sender, void *ptr, void *data)
        {
            sampler_ui *_this = static_cast<sampler_ui *>(ptr);
            LSPString path;
            status_t res = _this->wSfzImport->selected_file()->format(&path);
            if (res == STATUS_OK)
            {
                io::Path xpath;
                if ((res = xpath.set(&path)) != STATUS_OK)
                    return res;
                res = _this->import_sfz_file(NULL, &xpath);
            }
            return STATUS_OK;
        }

        status_t sampler_ui::slot_fetch_sfz_path(tk::Widget *sender, void *ptr, void *data)
        {
            sampler_ui *_this = static_cast<sampler_ui *>(ptr);
            if (_this == NULL)
                return STATUS_BAD_STATE;

            _this->init_path(sender, _this->pSfzPath, _this->pSfzFileType);
            return STATUS_OK;
        }

        status_t sampler_ui::slot_commit_sfz_path(tk::Widget *sender, void *ptr, void *data)
        {
            sampler_ui *_this = static_cast<sampler_ui *>(ptr);
            if (_this == NULL)
                return STATUS_BAD_STATE;

            _this->commit_path(sender, _this->pSfzPath, _this->pSfzFileType);
            return STATUS_OK;
        }

        status_t sampler_ui::slot_drag_request(tk::Widget *sender, void *ptr, void *data)
        {
            sampler_ui *self = static_cast<sampler_ui *>(ptr);
            if (self == NULL)
                return STATUS_BAD_STATE;

            tk::Window *wnd     = self->pWrapper->window();
            if (wnd == NULL)
                return STATUS_BAD_STATE;
            tk::Display *dpy    = wnd->display();
            if (dpy == NULL)
                return STATUS_BAD_STATE;

            ws::rectangle_t r;
            wnd->get_rectangle(&r);

            const char * const *ctype = dpy->get_drag_mime_types();
            ssize_t idx = self->pDragInSink->select_mime_type(ctype);
            if (idx >= 0)
            {
                dpy->accept_drag(self->pDragInSink, ws::DRAG_COPY, &r);
                lsp_trace("Accepted drag");
            }

            return STATUS_OK;
        }

        status_t sampler_ui::import_sfz_file(const io::Path *base, const io::Path *path)
        {
            status_t res;
            lltl::parray<sfz_region_t> regions, processed;

            // Read regions from the SFZ file
            if ((res = read_regions(&regions, path)) != STATUS_OK)
                return res;
            lsp_finally { destroy_regions(&regions); };

            // Process each region
            for (size_t i=0, n=regions.size(); i<n; ++i)
            {
                sfz_region_t *r = regions.uget(i);
                if (r == NULL)
                    continue;

                // Skip record if no sample is defined
                if (!(r->flags & SFZ_SAMPLE))
                    continue;

                // Find out the key note
                if (!(r->flags & SFZ_KEY))
                {
                    if (r->flags & SFZ_PITCH_KEYCENTER)
                        r->key      = r->pitch_keycenter;
                    else if (r->flags & SFZ_LOKEY)
                        r->key      = (r->flags & SFZ_HIKEY) ? (r->lokey + r->hikey) / 2 : r->lokey;
                    else if (r->flags & SFZ_HIKEY)
                        r->key      = r->hikey;
                    else
                        continue; // No key defined, skip instrument
                }

                // Update the key according to the note_offset and octave_offset values
                r->key      = lsp_limit(r->key + r->note_offset + r->octave_offset * 12, 0, 127);

                // Check for 'lorand' and 'hirand' parameters
                if (!(r->flags & (SFZ_LOVEL | SFZ_HIVEL)))
                {
                    if (r->flags & (SFZ_LORAND | SFZ_HIRAND))
                    {
                        if (r->flags & SFZ_LORAND)
                        {
                            r->lovel    = lsp_limit(ssize_t(127 * r->lorand), 0, 127);
                            r->flags   |= SFZ_LOVEL;
                        }
                        if (r->flags & SFZ_HIRAND)
                        {
                            r->hivel    = lsp_limit(ssize_t(127 * r->hirand), 0, 127);
                            r->flags   |= SFZ_HIVEL;
                        }
                    }
                }

                // Set default values if not present
                if (!(r->flags & SFZ_LOVEL))
                    r->lovel    = 0;
                if (!(r->flags & SFZ_HIVEL))
                    r->hivel    = 127;
                if (!(r->flags & SFZ_TUNE))
                    r->tune     = 0;
                if (!(r->flags & SFZ_VOLUME))
                    r->volume   = 0.0f; // 0.0f dB

                // Add region to processed
                if (!processed.add(r))
                    return STATUS_NO_MEM;
            }

            // Sort processed regions according to the specified criteria
            processed.qsort(cmp_sfz_regions);

            // Reset settings to default
            if ((res = pWrapper->reset_settings()) != STATUS_OK)
                return res;

            // Apply the changes
            sfz_region_t *prev = NULL;
            int id = 0, sample = 0;

            for (size_t i=0, n=processed.size(); i<n; ++i)
            {
                sfz_region_t *r = processed.uget(i);
                if (r == NULL)
                    continue;

                // Switch to the next instrument/sample
                if (prev != NULL)
                {
                    if ((!r->group_label.equals(&prev->group_label)) ||
                        (r->key != prev->key))
                    {
                        // Number of instruments exceeded?
                        if ((++id) >= int(meta::sampler_metadata::INSTRUMENTS_MAX))
                            break;
                        sample  = 0;
                    }
                }
                prev        = r;

                // Set instrument settings
                if (sample == 0)
                {
                    int note    = r->key;
                    int octave  = (note / 12);
                    note       %= 12;

                    set_float_value(GAIN_AMP_0_DB, "imix_%d", id);      // instrument mix gain
                    set_float_value(0, "chan_%d", id);                  // MIDI channel
                    set_float_value(note, "note_%d", id);               // MIDI note
                    set_float_value(octave, "oct_%d", id);              // MIDI octave

                    // Set instrument name
                    core::KVTStorage *kvt = wrapper()->kvt_lock();
                    if (kvt != NULL)
                    {
                        lsp_finally { wrapper()->kvt_release(); };
                        set_kvt_instrument_name(kvt, id, r->group_label.get_utf8());
                    }
                }

                // Apply the changes to the sample
                if (sample < int(meta::sampler_metadata::SAMPLE_FILES))
                {
                    float pan_l = lsp_limit(-100.0f + r->pan, -100.0f, 100.0f);
                    float pan_r = lsp_limit(1100.0f + r->pan, -100.0f, 100.0f);
                    float gain  = dspu::db_to_gain(r->volume);
                    float pitch = 0.01f * r->tune;
                    float vel   = (r->hivel * 100.0f) / 127.0f;

                    set_float_value(pan_l, "pl_%d_%d", id, sample);                 // sample pan left
                    set_float_value(pan_r, "pr_%d_%d", id, sample);                 // sample pan right
                    set_path_value(r->sample.get_utf8(), "sf_%d_%d", id, sample);   // sample file
                    lsp_trace("sf_%d_%d = %s", id, sample, r->sample.get_utf8());
                    set_float_value(gain, "mk_%d_%d", id, sample);                  // makeup gain
                    set_float_value(vel, "vl_%d_%d", id, sample);                   // velocity
                    set_float_value(pitch, "pi_%d_%d", id, sample);                 // pitch
                }

                // Increment sample number
                ++sample;
            }

            return STATUS_OK;
        }

        ssize_t sampler_ui::cmp_sfz_regions(const sfz_region_t *a, const sfz_region_t *b)
        {
            // Take named instruments first
            if (a->group_label.is_empty())
            {
                if (!b->group_label.is_empty())
                    return -1;
            }
            else if (b->group_label.is_empty())
                return 1;

            ssize_t cmp = a->group_label.compare_to(&b->group_label);
            if (cmp != 0)
                return cmp;

            // Compare keys
            if (a->key < b->key)
                return -1;
            else if (a->key > b->key)
                return 1;

            // Compare velocity
            if (a->hivel < b->hivel)
                return -1;
            else if (a->hivel > b->hivel)
                return 1;

            // Compare associated file names
            return a->sample.compare_to(&b->sample);
        }

        void sampler_ui::set_ui_instrument_name(inst_name_t *inst, const LSPString *name)
        {
            if (inst->wEdit != NULL)
                inst->wEdit->text()->set_raw(name);

            tk::String *text = (inst->wListItem != NULL) ? inst->wListItem->text() : NULL;
            if (text != NULL)
            {
                expr::Parameters params;
                params.set_int("id", inst->nIndex + 1);
                params.set_string("name", name);

                if (name->length() > 0)
                    text->set("lists.sampler.inst.id_name", &params);
                else
                    text->set("lists.sampler.inst.id", &params);
            }

            // Deploy the value to the current selected instrument
            if ((wCurrentInstrument != NULL) && (pCurrentInstrument != NULL))
            {
                ssize_t selected = ssize_t(pCurrentInstrument->value());
                if (selected == ssize_t(inst->nIndex))
                    wCurrentInstrument->text()->set_raw(name);
            }
        }

        void sampler_ui::handle_file_drop(const LSPString *path)
        {
            lsp_trace("Dropped file: %s", path->get_native());

            io::Path io_path;
            if (io_path.set(path) != STATUS_OK)
                return;

            // Try Hydrogen file first
            status_t res = import_hydrogen_file(path);

            // Now try SFZ file
            if (res != STATUS_OK)
                res = import_sfz_file(NULL, &io_path);

            // Now try sampler bundle (LSPC) file
            if (res != STATUS_OK)
                res = import_sampler_bundle(&io_path);

            // Set regular file to audio sample
            if (res == STATUS_OK)
                return;

            if ((pCurrentInstrument == NULL) || (pCurrentSample == NULL))
                return;

            size_t inst = pCurrentInstrument->value();
            size_t sample = pCurrentSample->value();
            lsp_trace("Current instrument=%d, sample=%d", int(inst), int(sample));
            set_path_value(path->get_utf8(), "sf_%d_%d", inst, sample);   // sample file
        }

    } /* namespace plugui */
} /* namespace lsp */



