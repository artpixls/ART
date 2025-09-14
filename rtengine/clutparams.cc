/* -*- C++ -*-
 *
 *  This file is part of ART.
 *
 *  Copyright 2025 Alberto Griggio <alberto.griggio@gmail.com>
 *
 *  ART is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  ART is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with ART.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "clutparams.h"
#include "../rtgui/mydiagonalcurve.h"
#include "../rtgui/myflatcurve.h"

#include <vector>
#include <array>
#include <unordered_map>

namespace rtengine {

namespace {

bool set_int(cJSON *n, double &out)
{
    if (cJSON_IsNumber(n)) {
        int v = n->valuedouble;
        if (v == n->valuedouble) {
            out = v;
            return true;
        }
    }
    return false;
}


static const std::unordered_map<std::string, double> curvetypes = {
    {"Linear", double(DCT_Linear)},
    {"Spline", double(DCT_Spline)},
    {"CatmullRom", double(DCT_CatmullRom)},
    {"NURBS", double(DCT_NURBS)},
    {"Parametric", double(DCT_Parametric)},
    {"ControlPoints", double(FCT_MinMaxCPoints)}
};

} // namespace

/**
 * LUT parameters can be specified via JSON arrays, whose content depends on
 * the parameter type. The array must be at least of size 2; the first element
 * is a string containing the name of the parameter, and the second element is
 * its GUI label. The rest of the array has the following structure:
 *
 * - for "bool" parameters, the 3rd optional element specifies the default
 *   value; the 4th optional element instead is a "group name" for the GUI: if
 *   set, this will cause the control to appear under a collapsible panel with
 *   the given name in the GUI;
 *
 * - for "float" parameters, the array size must be at least 4 and at most 7.
 *   The 3rd and 4th elements are the minimum and maximum values for the
 *   GUI slider. The optional 5th element is the default value, the optional
 *   6th element the precision to use in the GUI (e.g. 0.01 will use 2 decimal
 *   digits in the GUI), and the optional last element is the GUI group name;
 *
 * - for "int" parameters, the array size must be at least 3 and at most 6.
 *   If the 3rd parameter is an array of strings, it is interpreted as a list
 *   of options in a choice menu, with values corresponding to their index in
 *   the array (i.e. the 1st option will give a value of 0, the 2nd a value of
 *   1, etc.). In this case, the array can contain at most 2 other elements,
 *   which are respectively the default value and the optional GUI group name.
 *   If the 3rd parameter is not an array of strings, then the array size must
 *   be at least 4, with the 3rd and 4th elements corresponding to the minimum
 *   and maximum values for the GUI slider. The optional 5th element is the
 *   default value, and the optional last element the GUI group name.
 *
 * - arrays of floats are used to represent curves as 1D LUTs. Both curve
 *   types supported by ART (i.e. "diagonal" and "flat") are available,
 *   depending on the parameter definition. The array size of the parameter
 *   definition must be at least 2 and at most 8. The 3rd parameter indicates
 *   the curve type: 0 for diagonal, 1 for flat, and 2 for periodic flat
 *   (e.g. like a hue curve in ART). If not given, it defaults to 0. The 4th
 *   parameter, if given, specifies the default value for the curve. This can
 *   either be 0 (i.e. an identity curve), or an array of floats defining the
 *   type of curve and its control points, in the format used by .rtc curve
 *   files. The 5th and 6th parameters can be used to define the gradients
 *   appearing at the bottom and left of the curves in the GUI. Finally, as
 *   for other parameter types, the last two optional elements are the GUI
 *   group name and tooltip string.
 *
 * If default values are not given, zero is used.
 */
bool CLUTParamDescriptor::fill_from_json(cJSON *root)
{
    if (!cJSON_IsArray(root)) {
        return false;
    }
    
    auto sz = cJSON_GetArraySize(root);
    if (sz < 2) {
        return false;
    }
    
    auto n = cJSON_GetArrayItem(root, 0);
    if (!cJSON_IsString(n)) {
        return false;
    }
    name = cJSON_GetStringValue(n);

    n = cJSON_GetArrayItem(root, 1);
    if (!cJSON_IsString(n)) {
        return false;
    }
    gui_name = cJSON_GetStringValue(n);
    gui_group = "";
    gui_step = 1;
    value_default = { 0 };

    const auto set_group_tooltip =
        [&](int i, int sz) -> bool
        {
            auto n = cJSON_GetArrayItem(root, i);
            if (!cJSON_IsString(n)) {
                return false;
            }
            gui_group = cJSON_GetStringValue(n);
            if (i+1 < sz) {
                n = cJSON_GetArrayItem(root, i+1);
                if (!cJSON_IsString(n)) {
                    return false;
                }
                gui_tooltip = cJSON_GetStringValue(n);
            }
            return true;
        };

    const auto set_gradient =
        [&](cJSON *n, std::vector<std::array<float, 4>> &out) -> bool
        {
            out.clear();
            if (cJSON_IsNumber(n) && n->valuedouble == 0) {
                return true;
            } else if (cJSON_IsArray(n)) {
                size_t k = cJSON_GetArraySize(n);
                for (size_t j = 0; j < k; ++j) {
                    auto g = cJSON_GetArrayItem(n, j);
                    if (!cJSON_IsArray(g) || cJSON_GetArraySize(g) != 4) {
                        return false;
                    }
                    out.emplace_back();
                    auto &arr = out.back();
                    for (size_t c = 0; c < 4; ++c) {
                        auto e = cJSON_GetArrayItem(g, c);
                        if (!cJSON_IsNumber(e)) {
                            return false;
                        }
                        arr[c] = e->valuedouble;
                    }
                }
                return true;
            }
            return false;
        };

    switch (type) {
    case CLUTParamType::PT_BOOL:
        if (sz == 2) {
            return true;
        } else if (sz >= 3 && sz <= 5) {
            n = cJSON_GetArrayItem(root, 2);
            if (cJSON_IsBool(n)) {
                value_default = { double(cJSON_IsTrue(n)) };
            }
            if (sz >= 4) {
                return set_group_tooltip(3, sz);
            } else {
                return true;
            }
        }
        break;
    case CLUTParamType::PT_FLOAT:
        if (sz >= 4 && sz <= 8) {
            n = cJSON_GetArrayItem(root, 2);
            if (cJSON_IsNumber(n)) {
                value_min = n->valuedouble;
            } else {
                return false;
            }
            n = cJSON_GetArrayItem(root, 3);
            if (cJSON_IsNumber(n)) {
                value_max = n->valuedouble;
            } else {
                return false;
            }
            if (sz >= 5) {
                n = cJSON_GetArrayItem(root, 4);
                if (cJSON_IsNumber(n)) {
                    value_default = { n->valuedouble };
                } else {
                    return false;
                }
                if (sz >= 6) {
                    n = cJSON_GetArrayItem(root, 5);
                    if (cJSON_IsNumber(n)) {
                        gui_step = n->valuedouble;
                    } else {
                        return false;
                    }
                } else {
                    gui_step = (value_max - value_min) / 100.0;
                }
                if (sz >= 7) {
                    return set_group_tooltip(6, sz);
                }
            }
            return true;
        }
        break;
    case CLUTParamType::PT_INT:
        if (sz >= 3 && sz <= 7) {
            n = cJSON_GetArrayItem(root, 2);
            if (cJSON_IsArray(n)) {
                for (int i = 0, k = cJSON_GetArraySize(n); i < k; ++i) {
                    auto v = cJSON_GetArrayItem(n, i);
                    if (!cJSON_IsString(v)) {
                        return false;
                    }
                    choices.push_back(cJSON_GetStringValue(v));
                }
                type = CLUTParamType::PT_CHOICE;
                if (sz >= 4) {
                    n = cJSON_GetArrayItem(root, 3);
                    if (!set_int(n, value_default[0])) {
                        return false;
                    }
                    return (sz == 4) || set_group_tooltip(4, sz);
                } else {
                    return (sz == 3);
                }
            } else if (sz >= 4) {
                if (!set_int(n, value_min)) {
                    return false;
                }
                n = cJSON_GetArrayItem(root, 3);
                if (!set_int(n, value_max)) {
                    return false;
                }
                if (sz >= 5) {
                    n = cJSON_GetArrayItem(root, 4);
                    if (!set_int(n, value_default[0])) {
                        return false;
                    }
                    if (sz >= 6) {
                        return set_group_tooltip(5, sz);
                    }
                }
                return true;
            } else {
                return false;
            }
        }
        break;
    case CLUTParamType::PT_CURVE:
        if (sz >= 3 && sz <= 8) {
            n = cJSON_GetArrayItem(root, 2);
            if (cJSON_IsNumber(n) && n->valuedouble == int(n->valuedouble)) {
                switch (int(n->valuedouble)) {
                case 0:
                    type = CLUTParamType::PT_CURVE;
                    break;
                case 1:
                    type = CLUTParamType::PT_FLATCURVE;
                    break;
                case 2:
                    type = CLUTParamType::PT_FLATCURVE_PERIODIC;
                    break;
                default:
                    return false;
                }
                if (sz >= 4) {
                    n = cJSON_GetArrayItem(root, 3);
                    if (cJSON_IsNumber(n) && n->valuedouble == 0) {
                        // 0 is a special case for a default curve
                        value_default = { 0 };
                    } else if (cJSON_IsArray(n)) {
                        value_default.clear();
                        for (int i = 0, k = cJSON_GetArraySize(n); i < k; ++i) {
                            auto v = cJSON_GetArrayItem(n, i);
                            double d = 0;
                            if (i == 0 && cJSON_IsString(v)) {
                                auto it = curvetypes.find(cJSON_GetStringValue(v));
                                if (it == curvetypes.end()) {
                                    return false;
                                } else {
                                    d = it->second;
                                }
                            } else if (cJSON_IsNumber(v)) {
                                d = v->valuedouble;
                            } else {
                                return false;
                            }
                            value_default.push_back(d);
                        }
                    } else {
                        return false;
                    }
                }
                if (sz >= 5) {
                    if (set_group_tooltip(4, sz)) {
                        return true;
                    }
                    n = cJSON_GetArrayItem(root, 4);
                    if (!set_gradient(n, gui_bottom_gradient)) {
                        return false;
                    }
                    if (sz >= 6) {
                        if (set_group_tooltip(5, sz)) {
                            return true;
                        }
                        n = cJSON_GetArrayItem(root, 5);
                        if (!set_gradient(n, gui_left_gradient)) {
                            return false;
                        }
                        if (sz >= 7) {
                            return set_group_tooltip(6, sz);
                        }
                    }
                }
                return true;
            } else {
                return false;
            }
        } else if (sz == 2) {
            return true;
        }
        break;
    default:
        return false;
    }

    return false;  
}


const std::vector<std::pair<std::string, Glib::ustring>> &CLUTParamDescriptorList::get_presets() const
{
    return presets_;
}


bool CLUTParamDescriptorList::apply_preset(const std::string &key, CLUTParamValueMap &out)
{
    auto it = presets_map_.find(key);
    if (it == presets_map_.end()) {
        return false;
    }

    for (auto &p : it->second) {
        out[p.first] = p.second;
    }

    return true;
}


bool CLUTParamDescriptorList::add_preset_from_json(cJSON *root)
{
    if (!cJSON_IsArray(root)) {
        return false;
    }
    
    auto sz = cJSON_GetArraySize(root);
    if (sz < 3) {
        return false;
    }
    
    auto n = cJSON_GetArrayItem(root, 0);
    if (!cJSON_IsString(n)) {
        return false;
    }
    std::string key = cJSON_GetStringValue(n);

    n = cJSON_GetArrayItem(root, 1);
    if (!cJSON_IsString(n)) {
        return false;
    }
    Glib::ustring gui_name = cJSON_GetStringValue(n);

    CLUTParamValueMap vmap;

    n = cJSON_GetArrayItem(root, 2);
    if (!cJSON_IsObject(n)) {
        return false;
    }

    std::unordered_map<std::string, CLUTParamType> tpmap;
    for (auto &d : *this) {
        tpmap[d.name] = d.type;
    }

    for (auto c = n->child; c; c = c->next) {
        std::vector<double> value;
        
        if (!c->string) {
            return false;
        }
        auto jt = tpmap.find(c->string);
        if (jt == tpmap.end()) {
            return false;
        }
        auto type = jt->second;
    
        switch (type) {
        case CLUTParamType::PT_BOOL:
            if (cJSON_IsBool(c)) {
                value = { double(cJSON_IsTrue(c)) };
            } else {
                return false;
            }
            break;
        case CLUTParamType::PT_FLOAT:
            if (cJSON_IsNumber(c)) {
                value.push_back(c->valuedouble);
            } else {
                return false;
            }
            break;
        case CLUTParamType::PT_INT:
        case CLUTParamType::PT_CHOICE:
            value.push_back(0);
            if (!set_int(c, value.back())) {
                return false;
            }
            break;
        case CLUTParamType::PT_CURVE:
        case CLUTParamType::PT_FLATCURVE:
        case CLUTParamType::PT_FLATCURVE_PERIODIC:
            if (cJSON_IsNumber(c) && c->valuedouble == 0) {
                // 0 is a special case for a default curve
                value = { 0 };
            } else if (cJSON_IsArray(c)) {
                for (int i = 0, k = cJSON_GetArraySize(c); i < k; ++i) {
                    auto v = cJSON_GetArrayItem(c, i);
                    double d = 0;
                    if (i == 0 && cJSON_IsString(v)) {
                        auto it = curvetypes.find(cJSON_GetStringValue(v));
                        if (it == curvetypes.end()) {
                            return false;
                        } else {
                            d = it->second;
                        }
                    } else if (cJSON_IsNumber(v)) {
                        d = v->valuedouble;
                    } else {
                        return false;
                    }
                    value.push_back(d);
                }
            } else {
                return false;
            }
            break;
        default:
            return false;
        }

        vmap[c->string] = value;
    }

    return add_preset(key, gui_name, vmap);
}


bool CLUTParamDescriptorList::add_preset(const std::string &key, const Glib::ustring &gui_name, const CLUTParamValueMap &value)
{
    if (presets_map_.find(key) != presets_map_.end()) {
        return false;
    }

    presets_.push_back(std::make_pair(key, gui_name));
    presets_map_[key] = value;

    return true;
}

} // namespace rtengine
