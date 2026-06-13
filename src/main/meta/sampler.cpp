/*
 * Copyright (C) 2026 Linux Studio Plugins Project <https://lsp-plug.in/>
 *           (C) 2026 Vladimir Sadovnikov <sadko4u@gmail.com>
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

#include <lsp-plug.in/plug-fw/meta/ports.h>
#include <lsp-plug.in/plug-fw/meta/registry.h>
#include <lsp-plug.in/shared/meta/developers.h>
#include <lsp-plug.in/common/status.h>
#include <private/meta/sampler.h>

#define LSP_PLUGINS_SAMPLER_VERSION_MAJOR                   1
#define LSP_PLUGINS_SAMPLER_VERSION_MINOR                   0
#define LSP_PLUGINS_SAMPLER_VERSION_MICRO                   38

#define LSP_PLUGINS_SAMPLER_VERSION  \
    LSP_MODULE_VERSION( \
        LSP_PLUGINS_SAMPLER_VERSION_MAJOR, \
        LSP_PLUGINS_SAMPLER_VERSION_MINOR, \
        LSP_PLUGINS_SAMPLER_VERSION_MICRO  \
    )

namespace lsp
{
    namespace meta
    {
        // Lisf of different revisions for adding controls
        #define REV_0       0
        #define REV_1       1

        //-------------------------------------------------------------------------
        // Sampler
        static const port_item_t sampler_sample_selectors[] =
        {
            { "1", "sampler.samp.1" },
            { "2", "sampler.samp.2" },
            { "3", "sampler.samp.3" },
            { "4", "sampler.samp.4" },
            { "5", "sampler.samp.5" },
            { "6", "sampler.samp.6" },
            { "7", "sampler.samp.7" },
            { "8", "sampler.samp.8" },
            { NULL, NULL }
        };

        static const port_item_t mute_groups[] =
        {
            { "None", "sampler.samp.none" },
            { "A", NULL },
            { "B", NULL },
            { "C", NULL },
            { "D", NULL },
            { "E", NULL },
            { "F", NULL },
            { "G", NULL },
            { "H", NULL },
            { "I", NULL },
            { "J", NULL },
            { "K", NULL },
            { "L", NULL },
            { "M", NULL },
            { "N", NULL },
            { "O", NULL },
            { "P", NULL },
            { "Q", NULL },
            { "R", NULL },
            { "S", NULL },
            { "T", NULL },
            { "U", NULL },
            { "V", NULL },
            { "W", NULL },
            { "X", NULL },
            { "Y", NULL },
            { "Z", NULL },
            { NULL, NULL }
        };

        #define R(x) { x, "sampler.inst." x }
        static const port_item_t sampler_x12_instruments[] =
        {
            R("01"), R("02"), R("03"), R("04"),
            R("05"), R("06"), R("07"), R("08"),
            R("09"), R("10"), R("11"), R("12"),
            { NULL, NULL }
        };

        static const port_item_t sampler_x24_instruments[] =
        {
            R("01"), R("02"), R("03"), R("04"),
            R("05"), R("06"), R("07"), R("08"),
            R("09"), R("10"), R("11"), R("12"),
            R("13"), R("14"), R("15"), R("16"),
            R("17"), R("18"), R("19"), R("20"),
            R("21"), R("22"), R("23"), R("24"),
            { NULL, NULL }
        };

        static const port_item_t sampler_x48_instruments[] =
        {
            R("01"), R("02"), R("03"), R("04"),
            R("05"), R("06"), R("07"), R("08"),
            R("09"), R("10"), R("11"), R("12"),
            R("13"), R("14"), R("15"), R("16"),
            R("17"), R("18"), R("19"), R("20"),
            R("21"), R("22"), R("23"), R("24"),
            R("25"), R("26"), R("27"), R("28"),
            R("29"), R("30"), R("31"), R("32"),
            R("33"), R("34"), R("35"), R("36"),
            R("37"), R("38"), R("39"), R("40"),
            R("41"), R("42"), R("43"), R("44"),
            R("45"), R("46"), R("47"), R("48"),
            { NULL, NULL }
        };
        #undef R

        static const port_item_t sampler_x12_mixer_lines[] =
        {
            { "Instruments", "sampler.instruments" },
            { "Mixer", "sampler.mixer" },
            { NULL, NULL }
        };

        static const port_item_t sampler_x24_mixer_lines[] =
        {
            { "Instruments", "sampler.instruments" },
            { "Mixer 1-12",  "sampler.mixer_1:12" },
            { "Mixer 13-24", "sampler.mixer_13:24" },
            { NULL, NULL }
        };

        static const port_item_t sampler_x48_mixer_lines[] =
        {
            { "Instruments", "sampler.instruments" },
            { "Mixer 1-12",  "sampler.mixer_1:12" },
            { "Mixer 13-24", "sampler.mixer_13:24" },
            { "Mixer 25-36", "sampler.mixer_25:36" },
            { "Mixer 37-48", "sampler.mixer_37:48" },
            { NULL, NULL }
        };

        static const port_item_t sampler_crossfade_type[] =
        {
            { "Linear",         "fade.linear"      },
            { "Const Power",    "fade.const_power" },
            { NULL, NULL }
        };

        static const port_item_t sampler_loop_mode[] =
        {
            { "Simple: Direct",     "sampler.loop.simple.direct"            },
            { "Simple: Reverse",    "sampler.loop.simple.reverse"           },
            { "PP: Half Direct",    "sampler.loop.ping_pong.direct_half"    },
            { "PP: Half Reverse",   "sampler.loop.ping_pong.reverse_half"   },
            { "PP: Full Direct",    "sampler.loop.ping_pong.direct_full"    },
            { "PP: Full Reverse",   "sampler.loop.ping_pong.reverse_full"   },
            { "PP: Smart Direct",   "sampler.loop.ping_pong.direct_smart"   },
            { "PP: Smart Reverse",  "sampler.loop.ping_pong.reverse_smart"  },
            { NULL, NULL }
        };

        static const port_item_t sampler_sample_editor_tabs[] =
        {
            { "Main",           "sampler.edit.main"      },
            { "Pitch",          "sampler.edit.pitch"     },
            { "Stretch",        "sampler.edit.stretch"   },
            { "Envelope",       "sampler.edit.envelope"  },
            { "Loop",           "sampler.edit.loop"      },
            { NULL, NULL }
        };

        static const port_item_t sampler_midi_channels[] =
        {
            { "01",             "sampler.midi_channels.1" },
            { "02",             "sampler.midi_channels.2" },
            { "03",             "sampler.midi_channels.3" },
            { "04",             "sampler.midi_channels.4" },
            { "05",             "sampler.midi_channels.5" },
            { "06",             "sampler.midi_channels.6" },
            { "07",             "sampler.midi_channels.7" },
            { "08",             "sampler.midi_channels.8" },
            { "09",             "sampler.midi_channels.9" },
            { "10",             "sampler.midi_channels.10" },
            { "11",             "sampler.midi_channels.11" },
            { "12",             "sampler.midi_channels.12" },
            { "13",             "sampler.midi_channels.13" },
            { "14",             "sampler.midi_channels.14" },
            { "15",             "sampler.midi_channels.15" },
            { "16",             "sampler.midi_channels.16" },
            { "All",            "sampler.midi_channels.all" },
            { NULL,             NULL }
        };

        static const port_item_t sampler_envelope_types[] =
        {
            { "Off",            "adsr.type.off"             },
            { "VLines",         "adsr.type.vline"           },
            { "DLines",         "adsr.type.dline"           },
            { "Cubic",          "adsr.type.cubic"           },
            { "Quad",           "adsr.type.quad"            },
            { "Exp",            "adsr.type.exp"             },
            { NULL,             NULL }
        };

        #define S_DO_GROUP_PORTS(i) \
            STEREO_PORT_GROUP_PORTS(dout_ ## i, "dol_" #i, "dor_" #i)

        #define S_DO_GROUP(id, sid) \
            { "direct_out_" #id, "Direct Output " sid,    GRP_STEREO,     PGF_OUT,    dout_ ## id ##_ports      }

        #define S_FILE_GAIN_MONO \
            AMP_GAIN10("mx", "Sample mix gain", NULL, 1.0f)
        #define S_FILE_GAIN_STEREO \
            PAN_CTL("pl", "Sample left channel panorama", NULL, -100.0f), \
            PAN_CTL("pr", "Sample right channel panorama", NULL, 100.0f)

        #define S_PORTS_GLOBAL      \
            BYPASS,                 \
            TRIGGER("mute", "Forced mute", "Mute"), \
            SWITCH("muting", "Mute on stop", "Muting", 1.0f), \
            SWITCH("noff", "Note-off handling", "Note off", 0.0f), \
            CONTROL("fout", "Note-off fadeout", "Note fade out", U_MSEC, sampler_metadata::FADEOUT), \
            DRY_GAIN(1.0f),         \
            WET_GAIN(1.0f),         \
            DRYWET(100.0f),         \
            OUT_GAIN, \
            COMBO("sets", "Sample Editor Tab Selection", "Tab selector", 0, sampler_sample_editor_tabs)

        #define S_DO_CONTROL \
            SWITCH("do_gain", "Apply gain to direct-out", "DOut gain on", 1.0f), \
            SWITCH("do_pan", "Apply panning to direct-out", "DOut pan on", 1.0f), \
            ADDON_SWITCH(REV_1, "do_lstn", "Pass listen events to direct-out", "DOut listen on", 0.0f)

        #define S_SAMPLE_FILE(gain)        \
            PATH("sf", "Sample file"), \
            CONTROL("pi", "Sample pitch", NULL, U_SEMITONES, sampler_metadata::SAMPLE_PITCH), \
            SWITCH("so", "Sample stretch enabled", NULL, 0.0f), \
            CONTROL("st", "Sample relative stretch time", NULL, U_MSEC, sampler_metadata::SAMPLE_STRETCH), \
            CONTROL("ss", "Sample stretch region start", NULL, U_MSEC, sampler_metadata::SAMPLE_LENGTH), \
            CONTROL("se", "Sample stretch region end", NULL, U_MSEC, sampler_metadata::SAMPLE_LENGTH), \
            CONTROL("sc", "Sample stretch chunk", NULL, U_MSEC, sampler_metadata::SAMPLE_STRETCH_CHUNK), \
            CONTROL("sx", "Sample stretch fade", NULL, U_PERCENT, sampler_metadata::SAMPLE_STRETCH_FADE), \
            COMBO("xt", "Sample stretch crossfade type", NULL, 1, sampler_crossfade_type), \
            SWITCH("lo", "Sample loop enabled", NULL, 0.0f), \
            COMBO("lm", "Sample loop mode", NULL, 0, sampler_loop_mode), \
            CONTROL("lb", "Sample loop region start", NULL, U_MSEC, sampler_metadata::SAMPLE_LENGTH), \
            CONTROL("le", "Sample loop region end", NULL, U_MSEC, sampler_metadata::SAMPLE_LENGTH), \
            CONTROL("ll", "Sample loop fade", NULL, U_MSEC, sampler_metadata::SAMPLE_LENGTH), \
            COMBO("lx", "Sample loop crossfade type", NULL, 1, sampler_crossfade_type), \
            CONTROL("hc", "Sample head cut", NULL, U_MSEC, sampler_metadata::SAMPLE_LENGTH), \
            CONTROL("tc", "Sample tail cut", NULL, U_MSEC, sampler_metadata::SAMPLE_LENGTH), \
            CONTROL("fi", "Sample fade in", NULL, U_MSEC, sampler_metadata::SAMPLE_LENGTH), \
            CONTROL("fo", "Sample fade out", NULL, U_MSEC, sampler_metadata::SAMPLE_LENGTH), \
            AMP_GAIN10("mk", "Sample makeup gain", NULL, 1.0f), \
            SWITCH("ee", "Sample envelope enable", NULL, 0.0f), \
            SWITCH("eh", "Sample envelope hold enable", NULL, 0.0f), \
            SWITCH("eb", "Sample envelope break enable", NULL, 0.0f), \
            PERCENTS("ta", "Sample attack time", NULL, 0.0f, 0.01f), \
            PERCENTS("th", "Sample hold time", NULL, 10.0f, 0.01f), \
            PERCENTS("td", "Sample decay time", NULL, 30.0f, 0.01f), \
            PERCENTS("ts", "Sample slope time", NULL, 50.0f, 0.01f), \
            PERCENTS("tr", "Sample release time", NULL, 80.0f, 0.01f), \
            PERCENTS("bl", "Sample break level", NULL, 40.0f, 0.05f), \
            PERCENTS("sl", "Sample sustain level", NULL, 60.0f, 0.05f), \
            PERCENTS("ca", "Sample attack curvature", NULL, 50.0f, 0.01f), \
            PERCENTS("cd", "Sample decay curvature", NULL, 50.0f, 0.01f), \
            PERCENTS("cs", "Sample slope curvature", NULL, 50.0f, 0.01f), \
            PERCENTS("cr", "Sample release curvature", NULL, 50.0f, 0.01f), \
            COMBO("ea", "Sample attack envelope", NULL, 5, sampler_envelope_types), \
            COMBO("ed", "Sample decay envelope", NULL, 5, sampler_envelope_types), \
            COMBO("es", "Sample slope envelope", NULL, 5, sampler_envelope_types), \
            COMBO("er", "Sample release envelope", NULL, 5, sampler_envelope_types), \
            LOW_CONTROL_ALL("vl", "Sample velocity max", NULL, U_PERCENT, 0.0f, 100.0f, 0.0f, 0.05), \
            CONTROL("pd", "Sample pre-delay", NULL, U_MSEC, sampler_metadata::PREDELAY), \
            SWITCH("on", "Sample enabled", NULL, 1.0f), \
            TRIGGER("ls", "Sample listen preview", NULL), \
            TRIGGER("lc", "Sample stop listen preview", NULL), \
            SWITCH("rr", "Sample pre-reverse", NULL, 0.0f), \
            SWITCH("rs", "Sample post-reverse", NULL, 0.0f), \
            SWITCH("pc", "Sample auto-compensate", NULL, 0.0f), \
            CONTROL("xx", "Sample auto-compensate fade", NULL, U_PERCENT, sampler_metadata::SAMPLE_COMPENSATE_FADE), \
            CONTROL("cc", "Sample auto-compensate stretch chunk", NULL, U_MSEC, sampler_metadata::SAMPLE_COMPENSATE_CHUNK), \
            COMBO("xc", "Sample auto-compensate crossfade type", NULL, 1, sampler_crossfade_type), \
            gain, \
            BLINK("ac", "Sample activity"), \
            METER("pp", "Sample play position", U_MSEC, sampler_metadata::SAMPLE_PLAYBACK), \
            BLINK("no", "Sample note on event"), \
            METER("fl", "Length of loaded sample", U_MSEC, sampler_metadata::SAMPLE_LENGTH), \
            METER("al", "Actual length of loaded sample", U_MSEC, sampler_metadata::SAMPLE_LENGTH), \
            STATUS("fs", "Sample load status"), \
            MESH("fd", "Sample file contents", sampler_metadata::TRACKS_MAX, sampler_metadata::MESH_SIZE)

        #define S_INSTRUMENT(sample)    \
            COMBO("chan", "Channel", "Channel", sampler_metadata::CHANNEL_DFL, sampler_midi_channels), \
            COMBO("note", "Note", "Note", sampler_metadata::NOTE_DFL, notes), \
            COMBO("oct", "Octave", "Octave", sampler_metadata::OCTAVE_DFL, octaves), \
            INT_METER_ALL("mn", "MIDI Note #", U_NONE, 0, 127, 0, 1), \
            TRIGGER("trg", "Instrument listen preview", "Inst play"), \
            TRIGGER("stop", "Stop instrument listen preview", "Inst stop"), \
            CONTROL("dyna", "Dynamics", "Dynamics", U_PERCENT, sampler_metadata::DYNA), \
            CONTROL("drft", "Time drifting", "Drifting", U_MSEC, sampler_metadata::DRIFT), \
            SWITCH("hvel", "Velocity handling", "Velocity on", 1.0f), \
            PORT_SET("ssel", "Sample selector", sampler_sample_selectors, sample)

        #define S_MG_INSTRUMENT(sample)    \
            COMBO("chan", "Channel", NULL, sampler_metadata::CHANNEL_DFL, sampler_midi_channels), \
            COMBO("note", "Note", NULL, sampler_metadata::NOTE_DFL, notes), \
            COMBO("oct", "Octave", NULL, sampler_metadata::OCTAVE_DFL, octaves), \
            COMBO("mgrp", "Mute Group", NULL, 0, mute_groups), \
            SWITCH("mtg", "Mute on stop", NULL, 0.0f), \
            SWITCH("nto", "Note-off handling", NULL, 0.0f), \
            INT_METER_ALL("mn", "MIDI Note #", U_NONE, 0, 127, 0, 1), \
            TRIGGER("trg", "Instrument listen preview", NULL), \
            TRIGGER("stop", "Stop instrument listen preview", NULL), \
            CONTROL("dyna", "Dynamics", NULL, U_PERCENT, sampler_metadata::DYNA), \
            CONTROL("drft", "Time drifting", NULL, U_MSEC, sampler_metadata::DRIFT), \
            SWITCH("hvel", "Velocity handling", NULL, 1.0f), \
            PORT_SET("ssel", "Sample selector", sampler_sample_selectors, sample)

        #define S_AREA_SELECTOR(list)     \
            COMBO("msel", "Area selector", "Area", 0, list)

        #define S_INSTRUMENT_SELECTOR(list)     \
            PORT_SET("inst", "Instrument selector", list, sampler_multiple_ports)

        #define S_MIXER(id, sid)                      \
            SWITCH("ion_" #id, "Instrument " sid " on", "Inst " sid " on", 1.0f), \
            AMP_GAIN10("imix_" #id, "Instrument " sid " mix gain", "Inst " sid " gain", 1.0f), \
            PAN_CTL("panl_" #id, "Instrument " sid " pan left", "Inst " sid " pan L", -100.0f), \
            PAN_CTL("panr_" #id, "Instrument " sid " pan right", "Inst " sid " pan R", 100.0f), \
            BLINK("iact_" #id, "Instrument " sid " activity")

        #define S_DIRECT_OUT(id, sid)           \
            S_MIXER(id, sid),                        \
            SWITCH("don_" #id, "Direct output " sid " on", "DOut " sid " on", 1.0f), \
            AUDIO_OUTPUT("dol_" #id, "Direct output " sid " left"), \
            AUDIO_OUTPUT("dor_" #id, "Direct output " sid " right")

        // Define stereo direct outputs
        S_DO_GROUP_PORTS(0);
        S_DO_GROUP_PORTS(1);
        S_DO_GROUP_PORTS(2);
        S_DO_GROUP_PORTS(3);
        S_DO_GROUP_PORTS(4);
        S_DO_GROUP_PORTS(5);
        S_DO_GROUP_PORTS(6);
        S_DO_GROUP_PORTS(7);
        S_DO_GROUP_PORTS(8);
        S_DO_GROUP_PORTS(9);
        S_DO_GROUP_PORTS(10);
        S_DO_GROUP_PORTS(11);
        S_DO_GROUP_PORTS(12);
        S_DO_GROUP_PORTS(13);
        S_DO_GROUP_PORTS(14);
        S_DO_GROUP_PORTS(15);
        S_DO_GROUP_PORTS(16);
        S_DO_GROUP_PORTS(17);
        S_DO_GROUP_PORTS(18);
        S_DO_GROUP_PORTS(19);
        S_DO_GROUP_PORTS(20);
        S_DO_GROUP_PORTS(21);
        S_DO_GROUP_PORTS(22);
        S_DO_GROUP_PORTS(23);
        S_DO_GROUP_PORTS(24);
        S_DO_GROUP_PORTS(25);
        S_DO_GROUP_PORTS(26);
        S_DO_GROUP_PORTS(27);
        S_DO_GROUP_PORTS(28);
        S_DO_GROUP_PORTS(29);
        S_DO_GROUP_PORTS(30);
        S_DO_GROUP_PORTS(31);
        S_DO_GROUP_PORTS(32);
        S_DO_GROUP_PORTS(33);
        S_DO_GROUP_PORTS(34);
        S_DO_GROUP_PORTS(35);
        S_DO_GROUP_PORTS(36);
        S_DO_GROUP_PORTS(37);
        S_DO_GROUP_PORTS(38);
        S_DO_GROUP_PORTS(39);
        S_DO_GROUP_PORTS(40);
        S_DO_GROUP_PORTS(41);
        S_DO_GROUP_PORTS(42);
        S_DO_GROUP_PORTS(43);
        S_DO_GROUP_PORTS(44);
        S_DO_GROUP_PORTS(45);
        S_DO_GROUP_PORTS(46);
        S_DO_GROUP_PORTS(47);

        // Define Direct-output port groups
        const port_group_t sampler_x12_port_groups[] =
        {
            { "stereo_in",  "Stereo Input",     GRP_STEREO,     PGF_IN | PGF_MAIN,  stereo_in_group_ports       },
            { "stereo_out", "Stereo Output",    GRP_STEREO,     PGF_OUT | PGF_MAIN, stereo_out_group_ports      },
            S_DO_GROUP( 0, "01"),
            S_DO_GROUP( 1, "02"),
            S_DO_GROUP( 2, "03"),
            S_DO_GROUP( 3, "04"),
            S_DO_GROUP( 4, "05"),
            S_DO_GROUP( 5, "06"),
            S_DO_GROUP( 6, "07"),
            S_DO_GROUP( 7, "08"),
            S_DO_GROUP( 8, "09"),
            S_DO_GROUP( 9, "10"),
            S_DO_GROUP(10, "11"),
            S_DO_GROUP(11, "12"),
            { NULL, NULL }
        };

        const port_group_t sampler_x24_port_groups[] =
        {
            { "stereo_in",  "Stereo Input",     GRP_STEREO,     PGF_IN | PGF_MAIN,  stereo_in_group_ports       },
            { "stereo_out", "Stereo Output",    GRP_STEREO,     PGF_OUT | PGF_MAIN, stereo_out_group_ports      },
            S_DO_GROUP( 0, "01"),
            S_DO_GROUP( 1, "02"),
            S_DO_GROUP( 2, "03"),
            S_DO_GROUP( 3, "04"),
            S_DO_GROUP( 4, "05"),
            S_DO_GROUP( 5, "06"),
            S_DO_GROUP( 6, "07"),
            S_DO_GROUP( 7, "08"),
            S_DO_GROUP( 8, "09"),
            S_DO_GROUP( 9, "10"),
            S_DO_GROUP(10, "11"),
            S_DO_GROUP(11, "12"),
            S_DO_GROUP(12, "13"),
            S_DO_GROUP(13, "14"),
            S_DO_GROUP(14, "15"),
            S_DO_GROUP(15, "16"),
            S_DO_GROUP(16, "17"),
            S_DO_GROUP(17, "18"),
            S_DO_GROUP(18, "19"),
            S_DO_GROUP(19, "20"),
            S_DO_GROUP(20, "21"),
            S_DO_GROUP(21, "22"),
            S_DO_GROUP(22, "23"),
            S_DO_GROUP(23, "24"),
            { NULL, NULL }
        };

        const port_group_t sampler_x48_port_groups[] =
        {
            { "stereo_in",  "Stereo Input",     GRP_STEREO,     PGF_IN | PGF_MAIN,  stereo_in_group_ports       },
            { "stereo_out", "Stereo Output",    GRP_STEREO,     PGF_OUT | PGF_MAIN, stereo_out_group_ports      },
            S_DO_GROUP( 0, "01"),
            S_DO_GROUP( 1, "02"),
            S_DO_GROUP( 2, "03"),
            S_DO_GROUP( 3, "04"),
            S_DO_GROUP( 4, "05"),
            S_DO_GROUP( 5, "06"),
            S_DO_GROUP( 6, "07"),
            S_DO_GROUP( 7, "08"),
            S_DO_GROUP( 8, "09"),
            S_DO_GROUP( 9, "10"),
            S_DO_GROUP(10, "11"),
            S_DO_GROUP(11, "12"),
            S_DO_GROUP(12, "13"),
            S_DO_GROUP(13, "14"),
            S_DO_GROUP(14, "15"),
            S_DO_GROUP(15, "16"),
            S_DO_GROUP(16, "17"),
            S_DO_GROUP(17, "18"),
            S_DO_GROUP(18, "19"),
            S_DO_GROUP(19, "20"),
            S_DO_GROUP(20, "21"),
            S_DO_GROUP(21, "22"),
            S_DO_GROUP(22, "23"),
            S_DO_GROUP(23, "24"),
            S_DO_GROUP(24, "25"),
            S_DO_GROUP(25, "26"),
            S_DO_GROUP(26, "27"),
            S_DO_GROUP(27, "28"),
            S_DO_GROUP(28, "29"),
            S_DO_GROUP(29, "30"),
            S_DO_GROUP(30, "31"),
            S_DO_GROUP(31, "32"),
            S_DO_GROUP(32, "33"),
            S_DO_GROUP(33, "34"),
            S_DO_GROUP(34, "35"),
            S_DO_GROUP(35, "36"),
            S_DO_GROUP(36, "37"),
            S_DO_GROUP(37, "38"),
            S_DO_GROUP(38, "39"),
            S_DO_GROUP(39, "40"),
            S_DO_GROUP(40, "41"),
            S_DO_GROUP(41, "42"),
            S_DO_GROUP(42, "43"),
            S_DO_GROUP(43, "44"),
            S_DO_GROUP(44, "45"),
            S_DO_GROUP(45, "46"),
            S_DO_GROUP(46, "47"),
            S_DO_GROUP(47, "48"),
            { NULL, NULL }
        };

        // Define port sets
        static const port_t sample_file_mono_ports[] =
        {
            S_SAMPLE_FILE(S_FILE_GAIN_MONO),
            PORTS_END
        };

        static const port_t sample_file_stereo_ports[] =
        {
            S_SAMPLE_FILE(S_FILE_GAIN_STEREO),
            PORTS_END
        };

        static const port_t sampler_multiple_ports[] =
        {
            S_MG_INSTRUMENT(sample_file_stereo_ports),
            PORTS_END
        };

        static const int plugin_classes[]           = { C_INSTRUMENT, -1 };
        static const int clap_features_mono[]       = { CF_INSTRUMENT, CF_SAMPLER, CF_DRUM_MACHINE, CF_MONO, -1 };
        static const int clap_features_stereo[]     = { CF_INSTRUMENT, CF_SAMPLER, CF_DRUM_MACHINE, CF_STEREO, -1 };

        // Define port lists for each plugin
        static const port_t sampler_mono_ports[] =
        {
            PORTS_MONO_PLUGIN,
            PORTS_MIDI_CHANNEL,
            S_PORTS_GLOBAL,
            S_INSTRUMENT(sample_file_mono_ports),

            PORTS_END
        };

        static const port_t sampler_stereo_ports[] =
        {
            PORTS_STEREO_PLUGIN,
            PORTS_MIDI_CHANNEL,
            S_PORTS_GLOBAL,
            S_INSTRUMENT(sample_file_stereo_ports),

            PORTS_END
        };

        static const port_t sampler_x12_ports[] =
        {
            PORTS_STEREO_PLUGIN,
            PORTS_MIDI_CHANNEL,
            S_PORTS_GLOBAL,
            S_AREA_SELECTOR(sampler_x12_mixer_lines),
            S_INSTRUMENT_SELECTOR(sampler_x12_instruments),
            S_MIXER( 0, "01"),
            S_MIXER( 1, "02"),
            S_MIXER( 2, "03"),
            S_MIXER( 3, "04"),
            S_MIXER( 4, "05"),
            S_MIXER( 5, "06"),
            S_MIXER( 6, "07"),
            S_MIXER( 7, "08"),
            S_MIXER( 8, "09"),
            S_MIXER( 9, "10"),
            S_MIXER(10, "11"),
            S_MIXER(11, "12"),

            PORTS_END
        };

        static const port_t sampler_x24_ports[] =
        {
            PORTS_STEREO_PLUGIN,
            PORTS_MIDI_CHANNEL,
            S_PORTS_GLOBAL,
            S_AREA_SELECTOR(sampler_x24_mixer_lines),
            S_INSTRUMENT_SELECTOR(sampler_x24_instruments),
            S_MIXER( 0, "01"),
            S_MIXER( 1, "02"),
            S_MIXER( 2, "03"),
            S_MIXER( 3, "04"),
            S_MIXER( 4, "05"),
            S_MIXER( 5, "06"),
            S_MIXER( 6, "07"),
            S_MIXER( 7, "08"),
            S_MIXER( 8, "09"),
            S_MIXER( 9, "10"),
            S_MIXER(10, "11"),
            S_MIXER(11, "12"),
            S_MIXER(12, "13"),
            S_MIXER(13, "14"),
            S_MIXER(14, "15"),
            S_MIXER(15, "16"),
            S_MIXER(16, "17"),
            S_MIXER(17, "18"),
            S_MIXER(18, "19"),
            S_MIXER(19, "20"),
            S_MIXER(20, "21"),
            S_MIXER(21, "22"),
            S_MIXER(22, "23"),
            S_MIXER(23, "24"),

            PORTS_END
        };

        static const port_t sampler_x48_ports[] =
        {
            PORTS_STEREO_PLUGIN,
            PORTS_MIDI_CHANNEL,
            S_PORTS_GLOBAL,
            S_AREA_SELECTOR(sampler_x48_mixer_lines),
            S_INSTRUMENT_SELECTOR(sampler_x48_instruments),
            S_MIXER( 0, "01"),
            S_MIXER( 1, "02"),
            S_MIXER( 2, "03"),
            S_MIXER( 3, "04"),
            S_MIXER( 4, "05"),
            S_MIXER( 5, "06"),
            S_MIXER( 6, "07"),
            S_MIXER( 7, "08"),
            S_MIXER( 8, "09"),
            S_MIXER( 9, "10"),
            S_MIXER(10, "11"),
            S_MIXER(11, "12"),
            S_MIXER(12, "13"),
            S_MIXER(13, "14"),
            S_MIXER(14, "15"),
            S_MIXER(15, "16"),
            S_MIXER(16, "17"),
            S_MIXER(17, "18"),
            S_MIXER(18, "19"),
            S_MIXER(19, "20"),
            S_MIXER(20, "21"),
            S_MIXER(21, "22"),
            S_MIXER(22, "23"),
            S_MIXER(23, "24"),
            S_MIXER(24, "25"),
            S_MIXER(25, "26"),
            S_MIXER(26, "27"),
            S_MIXER(27, "28"),
            S_MIXER(28, "29"),
            S_MIXER(29, "30"),
            S_MIXER(30, "31"),
            S_MIXER(31, "32"),
            S_MIXER(32, "33"),
            S_MIXER(33, "34"),
            S_MIXER(34, "35"),
            S_MIXER(35, "36"),
            S_MIXER(36, "37"),
            S_MIXER(37, "38"),
            S_MIXER(38, "39"),
            S_MIXER(39, "40"),
            S_MIXER(40, "41"),
            S_MIXER(41, "42"),
            S_MIXER(42, "43"),
            S_MIXER(43, "44"),
            S_MIXER(44, "45"),
            S_MIXER(45, "46"),
            S_MIXER(46, "47"),
            S_MIXER(47, "48"),

            PORTS_END
        };

        static const port_t sampler_x12_do_ports[] =
        {
            PORTS_STEREO_PLUGIN,
            PORTS_MIDI_CHANNEL,
            S_PORTS_GLOBAL,
            S_DO_CONTROL,
            S_AREA_SELECTOR(sampler_x12_mixer_lines),
            S_INSTRUMENT_SELECTOR(sampler_x12_instruments),
            S_DIRECT_OUT( 0, "01"),
            S_DIRECT_OUT( 1, "02"),
            S_DIRECT_OUT( 2, "03"),
            S_DIRECT_OUT( 3, "04"),
            S_DIRECT_OUT( 4, "05"),
            S_DIRECT_OUT( 5, "06"),
            S_DIRECT_OUT( 6, "07"),
            S_DIRECT_OUT( 7, "08"),
            S_DIRECT_OUT( 8, "09"),
            S_DIRECT_OUT( 9, "10"),
            S_DIRECT_OUT(10, "11"),
            S_DIRECT_OUT(11, "12"),

            PORTS_END
        };

        static const port_t sampler_x24_do_ports[] =
        {
            PORTS_STEREO_PLUGIN,
            PORTS_MIDI_CHANNEL,
            S_PORTS_GLOBAL,
            S_DO_CONTROL,
            S_AREA_SELECTOR(sampler_x24_mixer_lines),
            S_INSTRUMENT_SELECTOR(sampler_x24_instruments),
            S_DIRECT_OUT( 0, "01"),
            S_DIRECT_OUT( 1, "02"),
            S_DIRECT_OUT( 2, "03"),
            S_DIRECT_OUT( 3, "04"),
            S_DIRECT_OUT( 4, "05"),
            S_DIRECT_OUT( 5, "06"),
            S_DIRECT_OUT( 6, "07"),
            S_DIRECT_OUT( 7, "08"),
            S_DIRECT_OUT( 8, "09"),
            S_DIRECT_OUT( 9, "10"),
            S_DIRECT_OUT(10, "11"),
            S_DIRECT_OUT(11, "12"),
            S_DIRECT_OUT(12, "13"),
            S_DIRECT_OUT(13, "14"),
            S_DIRECT_OUT(14, "15"),
            S_DIRECT_OUT(15, "16"),
            S_DIRECT_OUT(16, "17"),
            S_DIRECT_OUT(17, "18"),
            S_DIRECT_OUT(18, "19"),
            S_DIRECT_OUT(19, "20"),
            S_DIRECT_OUT(20, "21"),
            S_DIRECT_OUT(21, "22"),
            S_DIRECT_OUT(22, "23"),
            S_DIRECT_OUT(23, "24"),

            PORTS_END
        };

        static const port_t sampler_x48_do_ports[] =
        {
            PORTS_STEREO_PLUGIN,
            PORTS_MIDI_CHANNEL,
            S_PORTS_GLOBAL,
            S_DO_CONTROL,
            S_AREA_SELECTOR(sampler_x48_mixer_lines),
            S_INSTRUMENT_SELECTOR(sampler_x48_instruments),
            S_DIRECT_OUT( 0, "01"),
            S_DIRECT_OUT( 1, "02"),
            S_DIRECT_OUT( 2, "03"),
            S_DIRECT_OUT( 3, "04"),
            S_DIRECT_OUT( 4, "05"),
            S_DIRECT_OUT( 5, "06"),
            S_DIRECT_OUT( 6, "07"),
            S_DIRECT_OUT( 7, "08"),
            S_DIRECT_OUT( 8, "09"),
            S_DIRECT_OUT( 9, "10"),
            S_DIRECT_OUT(10, "11"),
            S_DIRECT_OUT(11, "12"),
            S_DIRECT_OUT(12, "13"),
            S_DIRECT_OUT(13, "14"),
            S_DIRECT_OUT(14, "15"),
            S_DIRECT_OUT(15, "16"),
            S_DIRECT_OUT(16, "17"),
            S_DIRECT_OUT(17, "18"),
            S_DIRECT_OUT(18, "19"),
            S_DIRECT_OUT(19, "20"),
            S_DIRECT_OUT(20, "21"),
            S_DIRECT_OUT(21, "22"),
            S_DIRECT_OUT(22, "23"),
            S_DIRECT_OUT(23, "24"),
            S_DIRECT_OUT(24, "25"),
            S_DIRECT_OUT(25, "26"),
            S_DIRECT_OUT(26, "27"),
            S_DIRECT_OUT(27, "28"),
            S_DIRECT_OUT(28, "29"),
            S_DIRECT_OUT(29, "30"),
            S_DIRECT_OUT(30, "31"),
            S_DIRECT_OUT(31, "32"),
            S_DIRECT_OUT(32, "33"),
            S_DIRECT_OUT(33, "34"),
            S_DIRECT_OUT(34, "35"),
            S_DIRECT_OUT(35, "36"),
            S_DIRECT_OUT(36, "37"),
            S_DIRECT_OUT(37, "38"),
            S_DIRECT_OUT(38, "39"),
            S_DIRECT_OUT(39, "40"),
            S_DIRECT_OUT(40, "41"),
            S_DIRECT_OUT(41, "42"),
            S_DIRECT_OUT(42, "43"),
            S_DIRECT_OUT(43, "44"),
            S_DIRECT_OUT(44, "45"),
            S_DIRECT_OUT(45, "46"),
            S_DIRECT_OUT(46, "47"),
            S_DIRECT_OUT(47, "48"),

            PORTS_END
        };

        const meta::bundle_t sampler_bundle =
        {
            "sampler",
            "Sampler",
            B_SAMPLERS,
            "GsNfZ0TF-bk",
            "This plugin implements single-note MIDI sample player with input and output.\nThere are up to eight samples available to play for different note velocities."
        };

        const meta::bundle_t multisampler_bundle =
        {
            "multisampler",
            "Multisampler",
            B_SAMPLERS,
            "GsNfZ0TF-bk",
            "This plugin implements multi-instrument MIDI sample player with stereo input\nand stereo output. For each instrument there are up to eight samples available\nto play for different note velocities. Also each instrument has it's own stereo\noutput that makes possible to record instrument outputs into individual tracks."
        };

        //-------------------------------------------------------------------------
        // Define plugin metadata
        const plugin_t sampler_mono =
        {
            "Klangerzeuger Mono",
            "Sampler Mono",
            "Sampler Mono",
            "KZ1M",
            &developers::v_sadovnikov,
            "sampler_mono",
            {
                LSP_LV2_URI("sampler_mono"),
                LSP_LV2UI_URI("sampler_mono"),
                "ca4r",
                LSP_VST3_UID("kz1m    ca4r"),
                LSP_VST3UI_UID("kz1m    ca4r"),
                0,
                NULL,
                LSP_CLAP_URI("sampler_mono"),
                LSP_GST_UID("sampler_mono"),
            },
            LSP_PLUGINS_SAMPLER_VERSION,
            plugin_classes,
            clap_features_mono,
            E_DUMP_STATE | E_FILE_PREVIEW,
            sampler_mono_ports,
            "plugins/sampling/single/mono.xml",
            NULL,
            mono_plugin_port_groups,
            &sampler_bundle,
            2
        };
        LSP_REGISTER_METADATA(sampler_mono);

        const plugin_t sampler_stereo =
        {
            "Klangerzeuger Stereo",
            "Sampler Stereo",
            "Sampler Stereo",
            "KZ1S",
            &developers::v_sadovnikov,
            "sampler_stereo",
            {
                LSP_LV2_URI("sampler_stereo"),
                LSP_LV2UI_URI("sampler_stereo"),
                "kjw3",
                LSP_VST3_UID("kz1s    kjw3"),
                LSP_VST3UI_UID("kz1s    kjw3"),
                0,
                NULL,
                LSP_CLAP_URI("sampler_stereo"),
                LSP_GST_UID("sampler_stereo"),
            },
            LSP_PLUGINS_SAMPLER_VERSION,
            plugin_classes,
            clap_features_stereo,
            E_DUMP_STATE | E_FILE_PREVIEW,
            sampler_stereo_ports,
            "plugins/sampling/single/stereo.xml",
            NULL,
            stereo_plugin_port_groups,
            &sampler_bundle,
            1
        };
        LSP_REGISTER_METADATA(sampler_stereo);

        const plugin_t multisampler_x12 =
        {
            "Schlagzeug x12 Stereo",
            "Multi-Sampler x12 Stereo",
            "Multi-Sampler x12 Stereo",
            "SZ12",
            &developers::v_sadovnikov,
            "multisampler_x12",
            {
                LSP_LV2_URI("multisampler_x12"),
                LSP_LV2UI_URI("multisampler_x12"),
                "clrs",
                LSP_VST3_UID("sz12    clrs"),
                LSP_VST3UI_UID("sz12    clrs"),
                0,
                NULL,
                LSP_CLAP_URI("multisampler_x12"),
                LSP_GST_UID("multisampler_x12"),
            },
            LSP_PLUGINS_SAMPLER_VERSION,
            plugin_classes,
            clap_features_stereo,
            E_DUMP_STATE | E_KVT_SYNC | E_FILE_PREVIEW,
            sampler_x12_ports,
            "plugins/sampling/multiple.xml",
            NULL,
            stereo_plugin_port_groups,
            &multisampler_bundle,
            3
        };
        LSP_REGISTER_METADATA(multisampler_x12);

        const plugin_t multisampler_x24 =
        {
            "Schlagzeug x24 Stereo",
            "Multi-Sampler x24 Stereo",
            "Multi-Sampler x24 Stereo",
            "SZ24",
            &developers::v_sadovnikov,
            "multisampler_x24",
            {
                LSP_LV2_URI("multisampler_x24"),
                LSP_LV2UI_URI("multisampler_x24"),
                "visl",
                LSP_VST3_UID("sz24    visl"),
                LSP_VST3UI_UID("sz24    visl"),
                0,
                NULL,
                LSP_CLAP_URI("multisampler_x24"),
                LSP_GST_UID("multisampler_x24"),
            },
            LSP_PLUGINS_SAMPLER_VERSION,
            plugin_classes,
            clap_features_stereo,
            E_DUMP_STATE | E_KVT_SYNC | E_FILE_PREVIEW,
            sampler_x24_ports,
            "plugins/sampling/multiple.xml",
            NULL,
            stereo_plugin_port_groups,
            &multisampler_bundle,
            4
        };
        LSP_REGISTER_METADATA(multisampler_x24);

        const plugin_t multisampler_x48 =
        {
            "Schlagzeug x48 Stereo",
            "Multi-Sampler x48 Stereo",
            "Multi-Sampler x48 Stereo",
            "SZ48",
            &developers::v_sadovnikov,
            "multisampler_x48",
            {
                LSP_LV2_URI("multisampler_x48"),
                LSP_LV2UI_URI("multisampler_x48"),
                "hnj4",
                LSP_VST3_UID("sz48    hnj4"),
                LSP_VST3UI_UID("sz48    hnj4"),
                0,
                NULL,
                LSP_CLAP_URI("multisampler_x48"),
                LSP_GST_UID("multisampler_x48"),
            },
            LSP_PLUGINS_SAMPLER_VERSION,
            plugin_classes,
            clap_features_stereo,
            E_DUMP_STATE | E_KVT_SYNC | E_FILE_PREVIEW,
            sampler_x48_ports,
            "plugins/sampling/multiple.xml",
            NULL,
            stereo_plugin_port_groups,
            &multisampler_bundle,
            5
        };
        LSP_REGISTER_METADATA(multisampler_x48);

        const plugin_t multisampler_x12_do =
        {
            "Schlagzeug x12 Direktausgabe",
            "Multi-Sampler x12 DirectOut",
            "Multi-Sampler x12 DirectOut",
            "SZ12D",
            &developers::v_sadovnikov,
            "multisampler_x12_do",
            {
                LSP_LV2_URI("multisampler_x12_do"),
                LSP_LV2UI_URI("multisampler_x12_do"),
                "7zkj",
                LSP_VST3_UID("sz12d   7zkj"),
                LSP_VST3UI_UID("sz12d   7zkj"),
                0,
                NULL,
                LSP_CLAP_URI("multisampler_x12_do"),
                LSP_GST_UID("multisampler_x12_do"),
            },
            LSP_PLUGINS_SAMPLER_VERSION,
            plugin_classes,
            clap_features_stereo,
            E_DUMP_STATE | E_KVT_SYNC | E_FILE_PREVIEW,
            sampler_x12_do_ports,
            "plugins/sampling/multiple.xml",
            NULL,
            sampler_x12_port_groups,
            &multisampler_bundle,
            6
        };
        LSP_REGISTER_METADATA(multisampler_x12_do);

        const plugin_t multisampler_x24_do =
        {
            "Schlagzeug x24 Direktausgabe",
            "Multi-Sampler x24 DirectOut",
            "Multi-Sampler x24 DirectOut",
            "SZ24D",
            &developers::v_sadovnikov,
            "multisampler_x24_do",
            {
                LSP_LV2_URI("multisampler_x24_do"),
                LSP_LV2UI_URI("multisampler_x24_do"),
                "vimj",
                LSP_VST3_UID("sz24d   vimj"),
                LSP_VST3UI_UID("sz24d   vimj"),
                0,
                NULL,
                LSP_CLAP_URI("multisampler_x24_do"),
                LSP_GST_UID("multisampler_x24_do"),
            },
            LSP_PLUGINS_SAMPLER_VERSION,
            plugin_classes,
            clap_features_stereo,
            E_DUMP_STATE | E_KVT_SYNC | E_FILE_PREVIEW,
            sampler_x24_do_ports,
            "plugins/sampling/multiple.xml",
            NULL,
            sampler_x24_port_groups,
            &multisampler_bundle,
            7
        };
        LSP_REGISTER_METADATA(multisampler_x24_do);

        const plugin_t multisampler_x48_do =
        {
            "Schlagzeug x48 Direktausgabe",
            "Multi-Sampler x48 DirectOut",
            "Multi-Sampler x48 DirectOut",
            "SZ48D",
            &developers::v_sadovnikov,
            "multisampler_x48_do",
            {
                LSP_LV2_URI("multisampler_x48_do"),
                LSP_LV2UI_URI("multisampler_x48_do"),
                "blyi",
                LSP_VST3_UID("sz48d   blyi"),
                LSP_VST3UI_UID("sz48d   blyi"),
                0,
                NULL,
                LSP_CLAP_URI("multisampler_x48_do"),
                LSP_GST_UID("multisampler_x48_do"),
            },
            LSP_PLUGINS_SAMPLER_VERSION,
            plugin_classes,
            clap_features_stereo,
            E_DUMP_STATE | E_KVT_SYNC | E_FILE_PREVIEW,
            sampler_x48_do_ports,
            "plugins/sampling/multiple.xml",
            NULL,
            sampler_x48_port_groups,
            &multisampler_bundle,
            8
        };
        LSP_REGISTER_METADATA(multisampler_x48_do);

    } /* namespace meta */
} /* namespace lsp */
