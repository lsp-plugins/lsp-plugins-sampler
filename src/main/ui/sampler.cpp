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
#include <private/ui/sampler.h>
#include <lsp-plug.in/plug-fw/ui.h>
#include <lsp-plug.in/plug-fw/meta/func.h>
#include <lsp-plug.in/io/Dir.h>
#include <lsp-plug.in/runtime/system.h>

namespace lsp
{
    namespace plugins
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

        static ui::Module *multisampler_factory_func(const meta::plugin_t *meta)
        {
            return new sampler_ui(meta);
        }

        // Use different factories for Mono/Stereo sampler and multisampler plugins
        static ui::Factory sampler_factory(sampler_uis, 2);
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
        sampler_ui::sampler_ui(const meta::plugin_t *meta):
            ui::Module(meta)
        {
            pHydrogenPath       = NULL;
            pCurrentInstrument  = NULL;
            wHydrogenImport     = NULL;
            wCurrentInstrument  = NULL;
        }

        sampler_ui::~sampler_ui()
        {
            // Will be automatically destroyed from list of widgets
            wHydrogenImport     = NULL;
            wCurrentInstrument  = NULL;

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

            // Destroy instrument descriptors
            vInstNames.flush();
        }

        status_t sampler_ui::post_init()
        {
            status_t res = ui::Module::post_init();
            if (res != STATUS_OK)
                return res;

            lookup_hydrogen_files();

            // Find hydrogen port
            pHydrogenPath   =  pWrapper->port(UI_CONFIG_PORT_PREFIX UI_DLG_HYDROGEN_PATH_ID);

            // Add subwidgets
            tk::Registry *widgets   = pWrapper->controller()->widgets();
            tk::Menu *menu          = tk::widget_cast<tk::Menu>(widgets->find(WUID_IMPORT_MENU));
            if (menu != NULL)
            {
                tk::MenuItem *child = new tk::MenuItem(pDisplay);
                widgets->add(child);
                child->init();
                child->text()->set("actions.import_hydrogen_drumkit_file");
                child->slots()->bind(tk::SLOT_SUBMIT, slot_start_import_hydrogen_file, this);
                menu->add(child);

                if (vDrumkits.size() > 0)
                {
                    // Create menu item
                    child           = new tk::MenuItem(pDisplay);
                    widgets->add(child);
                    child->init();
                    child->text()->set("actions.import_installed_hydrogen_drumkit");
                    menu->add(child);

                    // Create submenu
                    menu            = new tk::Menu(pDisplay);
                    widgets->add(menu);
                    menu->init();
                    child->menu()->set(menu);

                    // Add hydrogen files to menu
                    add_hydrogen_files_to_menu(menu);
                }
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

                inst->pWidget   = ed;
                inst->nIndex    = i;
                inst->bChanged  = false;
            }

            // Find widget and port associated with the current selected instrument
            pCurrentInstrument  = wrapper()->port("inst");
            wCurrentInstrument  = wrapper()->controller()->widgets()->get<tk::Edit>("iname");
            if (pCurrentInstrument != NULL)
                pCurrentInstrument->bind(this);
            if (wCurrentInstrument != NULL)
                wCurrentInstrument->slots()->bind(tk::SLOT_CHANGE, slot_instrument_name_updated, this);

            return STATUS_OK;
        }

        void sampler_ui::idle()
        {
            // Scan the list of instrument names for changes
            size_t changes = 0;
            for (size_t i=0, n=vInstNames.size(); i<n; ++i)
            {
                inst_name_t *name = vInstNames.uget(i);
                if ((name->pWidget) && (name->bChanged))
                    ++changes;
            }

            // Apply instrument names to KVT
            if (changes > 0)
            {
                core::KVTStorage *kvt = wrapper()->kvt_lock();
                if (kvt != NULL)
                {
                    LSPString value;

                    for (size_t i=0, n=vInstNames.size(); i<n; ++i)
                    {
                        inst_name_t *name = vInstNames.uget(i);
                        if ((!name->pWidget) || (!name->bChanged))
                            continue;

                        // Obtain the new instrument name
                        if (name->pWidget->text()->format(&value) != STATUS_OK)
                            continue;

                        // Submit new value to KVT
                        set_instrument_name(kvt, name->nIndex, value.get_utf8());
                    }

                    wrapper()->kvt_release();
                }
            }
        }

        status_t sampler_ui::reset_settings()
        {
            core::KVTStorage *kvt = wrapper()->kvt_lock();
            if (kvt != NULL)
            {
                // Reset all names for all instruments
                for (size_t i=0, n=vInstNames.size(); i<n; ++i)
                {
                    inst_name_t *name = vInstNames.uget(i);
                    if (!name->pWidget)
                        continue;
                    set_instrument_name(kvt, name->nIndex, "");
                    name->bChanged  = false;
                }

                wrapper()->kvt_release();
            }

            return STATUS_OK;
        }

        void sampler_ui::kvt_changed(core::KVTStorage *kvt, const char *id, const core::kvt_param_t *value)
        {
            if ((value->type == core::KVT_STRING) && (::strstr(id, "/instrument/") == id))
            {
                id += ::strlen("/instrument/");

                char *endptr = NULL;
                errno = 0;
                long index = ::strtol(id, &endptr, 10);

                // Valid object number?
                if ((errno == 0) && (!::strcmp(endptr, "/name")) && (index >= 0))
                {
                    for (size_t i=0, n=vInstNames.size(); i<n; ++i)
                    {
                        inst_name_t *name = vInstNames.uget(i);
                        if ((!name->pWidget) || (name->nIndex != size_t(index)))
                            continue;

                        name->pWidget->text()->set_raw(value->str);
                        name->bChanged = false;
                    }

                    // Deploy the value to the current selected instrument
                    if ((wCurrentInstrument != NULL) && (pCurrentInstrument != NULL))
                    {
                        ssize_t selected = ssize_t(pCurrentInstrument->value());
                        if (selected == index)
                            wCurrentInstrument->text()->set_raw(value->str);
                    }
                }
            }
        }

        ssize_t sampler_ui::cmp_drumkit_files(const h2drumkit_t *a, const h2drumkit_t *b)
        {
            return a->sName.compare_to_nocase(&b->sName);
        }

        status_t sampler_ui::add_drumkit(const io::Path *path, const hydrogen::drumkit_t *dk, bool system)
        {
            h2drumkit_t *drumkit = new h2drumkit_t();
            if (drumkit == NULL)
                return STATUS_NO_MEM;

            if (!drumkit->sName.set(&dk->name))
            {
                delete drumkit;
                return STATUS_NO_MEM;
            }

            if (drumkit->sPath.set(path) != STATUS_OK)
            {
                delete drumkit;
                return STATUS_NO_MEM;
            }

            drumkit->bSystem        = system;
            drumkit->pMenu          = NULL;

            if (!vDrumkits.add(drumkit))
            {
                delete drumkit;
                return STATUS_NO_MEM;
            }

            return STATUS_OK;
        }

        status_t sampler_ui::scan_hydrogen_directory(const io::Path *path, bool system)
        {
            status_t res;
            io::Path dir, subdir;
            io::fattr_t fa;

            // Open the directory
            if ((res = dir.set(path)) != STATUS_OK)
                return res;
            if ((res = dir.append_child("data/drumkits")) != STATUS_OK)
                return res;

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
                if ((res = add_drumkit(&subdir, &dk, system)) != STATUS_OK)
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
            io::Path dir, subdir;
            for (const char **path = h2_system_paths; (path != NULL) && (*path != NULL); ++path)
            {
                if (dir.set(*path) != STATUS_OK)
                    continue;

                scan_hydrogen_directory(&dir, true);
            }

            // Lookup in user's home directory
            if (system::get_home_directory(&dir) != STATUS_OK)
                return;

            for (const char **path = h2_user_paths; (path != NULL) && (*path != NULL); ++path)
            {
                if (subdir.set(&dir) != STATUS_OK)
                    continue;
                if (subdir.append_child(*path) != STATUS_OK)
                    continue;

                scan_hydrogen_directory(&subdir, false);
            }

            // Sort the result
            if (vDrumkits.size() >= 2)
                vDrumkits.qsort(cmp_drumkit_files);
        }

        void sampler_ui::add_hydrogen_files_to_menu(tk::Menu *menu)
        {
            LSPString tmp;

            for (size_t i=0, n=vDrumkits.size(); i<n; ++i)
            {
                h2drumkit_t *h2 = vDrumkits.uget(i);

                tk::MenuItem *child = new tk::MenuItem(pDisplay);
                pWrapper->controller()->widgets()->add(child);
                child->init();
                child->text()->set((h2->bSystem) ? "labels.file_display.system" : "labels.file_display.user");
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
                    lsp_trace("Importing Hydrogen file from %s", h2->sPath.as_utf8());
                    _this->import_hydrogen_file(h2->sPath.as_string());
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
                        ffi->extensions()->set("");
                    }

                    if ((ffi = f->add()) != NULL)
                    {
                        ffi->pattern()->set("*");
                        ffi->title()->set("files.all");
                        ffi->extensions()->set("");
                    }
                }

                dlg->slots()->bind(tk::SLOT_SUBMIT, slot_call_import_hydrogen_file, ptr);
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
                res = _this->import_hydrogen_file(&path);
            return STATUS_OK;
        }

        status_t sampler_ui::slot_fetch_hydrogen_path(tk::Widget *sender, void *ptr, void *data)
        {
            sampler_ui *_this = static_cast<sampler_ui *>(ptr);
            if ((_this == NULL) || (_this->pHydrogenPath == NULL))
                return STATUS_BAD_STATE;

            tk::FileDialog *dlg = tk::widget_cast<tk::FileDialog>(sender);
            if (dlg == NULL)
                return STATUS_OK;

            dlg->path()->set_raw(_this->pHydrogenPath->buffer<char>());
            return STATUS_OK;
        }

        status_t sampler_ui::slot_commit_hydrogen_path(tk::Widget *sender, void *ptr, void *data)
        {
            sampler_ui *_this = static_cast<sampler_ui *>(ptr);
            if ((_this == NULL) || (_this->pHydrogenPath == NULL))
                return STATUS_BAD_STATE;

            tk::FileDialog *dlg = tk::widget_cast<tk::FileDialog>(sender);
            if (dlg == NULL)
                return STATUS_OK;

            LSPString path;
            if ((dlg->path()->format(&path) == STATUS_OK))
            {
                const char *upath = path.get_utf8();
                _this->pHydrogenPath->write(upath, ::strlen(upath));
                _this->pHydrogenPath->notify_all();
            }

            return STATUS_OK;
        }

        status_t sampler_ui::slot_instrument_name_updated(tk::Widget *sender, void *ptr, void *data)
        {
            sampler_ui *_this = static_cast<sampler_ui *>(ptr);

            // Pass the changed value to the corresponding widget
            ssize_t selected =
                ((_this->pCurrentInstrument != NULL) && (_this->pCurrentInstrument != NULL)) ?
                    ssize_t(_this->pCurrentInstrument->value()) : -1;

            if ((sender != NULL) && (sender == _this->wCurrentInstrument))
            {
                // Mark the corresponding instrument name being changed
                for (size_t i=0, n=_this->vInstNames.size(); i<n; ++i)
                {
                    inst_name_t *name = _this->vInstNames.uget(i);
                    if ((ssize_t(name->nIndex) == selected) && (name->pWidget != NULL))
                    {
                        name->pWidget->text()->set(_this->wCurrentInstrument->text());
                        name->bChanged  = true;
                    }
                }
            }
            else
            {
                // Mark the corresponding instrument name being changed
                for (size_t i=0, n=_this->vInstNames.size(); i<n; ++i)
                {
                    inst_name_t *name = _this->vInstNames.uget(i);
                    if (name->pWidget != sender)
                        continue;

                    // Update the name of selected instrument
                    if (ssize_t(name->nIndex) == selected)
                        _this->wCurrentInstrument->text()->set(name->pWidget->text());
                    name->bChanged  = true;
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
                p->notify_all();
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
                p->notify_all();
            }

            va_end(v);
        }

        void sampler_ui::set_instrument_name(core::KVTStorage *kvt, int id, const char *name)
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
            }
            else
            {
                set_path_value("", "sf_%d_%d", id, jd);                     // sample file
                set_float_value(GAIN_AMP_0_DB, "mk_%d_%d", id, jd);         // makeup gain
                set_float_value((100.0f * (8 - jd)) / meta::sampler_metadata::SAMPLE_FILES, "vl_%d_%d", id, jd);    // velocity
            }

            set_float_value(1.0f, "on_%d_%d", id, jd);                      // enabled
            set_float_value(meta::sampler_metadata::SAMPLE_PITCH_DFL, "pi_%d_%d", id, jd);      // sample pitch
            set_float_value(meta::sampler_metadata::SAMPLE_STRETCH_DFL, "st_%d_%d", id, jd);    // stretch
            set_float_value(meta::sampler_metadata::SAMPLE_LENGTH_DFL, "ss_%d_%d", id, jd);     // stretch
            set_float_value(meta::sampler_metadata::SAMPLE_LENGTH_DFL, "se_%d_%d", id, jd);     // stretch
            set_float_value(meta::sampler_metadata::SAMPLE_STRETCH_CHUNK_DFL, "sc_%d_%d", id, jd);    // stretch
            set_float_value(meta::sampler_metadata::SAMPLE_STRETCH_FADE_DFL, "sx_%d_%d", id, jd);          // stretch
            set_float_value(meta::sampler_metadata::SAMPLE_LENGTH_DFL, "hc_%d_%d", id, jd);     // head cut
            set_float_value(meta::sampler_metadata::SAMPLE_LENGTH_DFL, "tc_%d_%d", id, jd);     // tail cut
            set_float_value(meta::sampler_metadata::SAMPLE_LENGTH_DFL, "fi_%d_%d", id, jd);     // fade in
            set_float_value(meta::sampler_metadata::SAMPLE_LENGTH_DFL, "fo_%d_%d", id, jd);     // fade out
            set_float_value(meta::sampler_metadata::PREDELAY_DFL, "pd_%d_%d", id, jd);          // pre-delay
            set_float_value(-100.0f, "pl_%d_%d", id, jd);                   // pan left
            set_float_value(+100.0f, "pr_%d_%d", id, jd);                   // pan right

            return STATUS_OK;
        }

        status_t sampler_ui::add_instrument(int id, const hydrogen::instrument_t *inst)
        {
            // Reset to defaults
            set_float_value(meta::sampler_metadata::CHANNEL_DFL, "chan_%d", id);    // channel
            set_float_value(meta::sampler_metadata::NOTE_DFL, "note_%d", id);       // note
            set_float_value(meta::sampler_metadata::OCTAVE_DFL, "oct_%d", id);      // octave
            set_float_value(0.0f, "mgrp_%d", id);                                   // mute group
            set_float_value(0.0f, "mtg_%d", id);                                    // mute on stop
            set_float_value(meta::sampler_metadata::DYNA_DFL, "dyna_%d", id);       // dynamics
            set_float_value(meta::sampler_metadata::DRIFT_DFL, "drft_%d", id);      // time drifting
            set_float_value(1.0f, "ion_%d", id);                                    // instrument on
            set_float_value(0.0f, "ssel_%d", id);                                   // sample selector

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
                set_instrument_name(kvt, id, (inst != NULL) ? inst->name.get_utf8() : "");
                wrapper()->kvt_release();
            }

            return STATUS_OK;
        }

        void sampler_ui::notify(ui::IPort *port)
        {
            if ((port != NULL) && (port == pCurrentInstrument) && (wCurrentInstrument != NULL))
            {
                core::KVTStorage *kvt = wrapper()->kvt_lock();
                if (kvt != NULL)
                {
                    const char *param = "";
                    char buf[0x40];
                    snprintf(buf, sizeof(buf), "/instrument/%d/name", int(pCurrentInstrument->value()));
                    if (kvt->get(buf, &param) != STATUS_OK)
                        param = "";
                    wCurrentInstrument->text()->set_raw(param);

                    wrapper()->kvt_release();
                }
            }
        }
    }
}



