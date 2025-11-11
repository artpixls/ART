/* -*- C++ -*-
 *  
 *  This file is part of RawTherapee.
 *
 *  Copyright (c) 2004-2010 Gabor Horvath <hgabor@rawtherapee.com>
 *
 *  RawTherapee is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  RawTherapee is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with RawTherapee.  If not, see <http://www.gnu.org/licenses/>.
 */
#pragma once

#include <cmath>
#include <cstdio>
#include <map>
#include <type_traits>
#include <vector>
#include <array>

#include <glibmm.h>
#include <lcms2.h>

#include "coord.h"
#include "noncopyable.h"
#include "../rtgui/paramsedited.h"
#include "clutparams.h"

class ParamsEdited;

namespace rtengine {

class ProgressListener;

enum RenderingIntent {
    RI_PERCEPTUAL = INTENT_PERCEPTUAL,
    RI_RELATIVE = INTENT_RELATIVE_COLORIMETRIC,
    RI_SATURATION = INTENT_SATURATION,
    RI_ABSOLUTE = INTENT_ABSOLUTE_COLORIMETRIC,
    RI__COUNT
};


namespace procparams {

class KeyFile {
public:
    explicit KeyFile(const Glib::ustring &prefix=""):
        prefix_(prefix), pl_(nullptr) {}
    void setProgressListener(ProgressListener *pl) { pl_ = pl; }
    ProgressListener *progressListener() const { return pl_; }
    
    bool has_group(const Glib::ustring &grp) const;
    bool has_key(const Glib::ustring &grp, const Glib::ustring &key) const;
    Glib::ArrayHandle<Glib::ustring> get_keys(const Glib::ustring &grp) const;
    
    Glib::ustring get_string(const Glib::ustring &grp, const Glib::ustring &key) const;
    int get_integer(const Glib::ustring &grp, const Glib::ustring &key) const;
    double get_double(const Glib::ustring &grp, const Glib::ustring &key) const;
    bool get_boolean(const Glib::ustring &grp, const Glib::ustring &key) const;
    Glib::ArrayHandle<Glib::ustring> get_string_list(const Glib::ustring &grp, const Glib::ustring &key) const;
    Glib::ArrayHandle<int> get_integer_list(const Glib::ustring &grp, const Glib::ustring &key) const;
    Glib::ArrayHandle<double> get_double_list(const Glib::ustring &grp, const Glib::ustring &key) const;
    
    void set_string(const Glib::ustring &grp, const Glib::ustring &key, const Glib::ustring& string);
    void set_boolean(const Glib::ustring &grp, const Glib::ustring &key, bool value);
    void set_integer(const Glib::ustring &grp, const Glib::ustring &key, int value);
    void set_double(const Glib::ustring &grp, const Glib::ustring &key, double value);
    void set_string_list(const Glib::ustring &grp, const Glib::ustring &key, const Glib::ArrayHandle<Glib::ustring> &list);
    void set_integer_list(const Glib::ustring &grp, const Glib::ustring &key, const Glib::ArrayHandle<int> &list);
    void set_double_list(const Glib::ustring &grp, const Glib::ustring &key, const Glib::ArrayHandle<double> &list);

    bool load_from_file(const Glib::ustring &fn);
    bool load_from_data(const Glib::ustring &data);
    Glib::ustring to_data();

    Glib::ustring get_prefix() const { return prefix_; }
    void set_prefix(const Glib::ustring &prefix) { prefix_ = prefix; }

    const Glib::ustring &filename() const { return filename_; }
    
private:
    Glib::ustring GRP(const Glib::ustring &g) const { return prefix_ + g; }
    
    Glib::ustring prefix_;
    Glib::KeyFile kf_;

    Glib::ustring filename_;
    mutable ProgressListener *pl_;
};


struct AreaMask {
    struct Shape {
        enum Mode {
            ADD,
            SUBTRACT,
            INTERSECT
        };
        enum Type {
            RECTANGLE,
            POLYGON,
            GRADIENT
        };
        Mode mode;
        double feather; // [0,100]
        double blur;

        Shape();
        virtual ~Shape() {}
        virtual Type getType() const = 0 ;
        virtual bool operator==(const Shape &other) const;
        virtual bool operator!=(const Shape &other) const;

        static int toImgSpace(double v, int imSize);
        static double toParamRange(int v, int imSize);
        virtual std::unique_ptr<Shape> clone() const = 0;
    };
    
    struct Rectangle: public Shape {
        double x; // [-100,100], with 0 as center of the image
        double y; // [-100,100]
        double width; // [0,200], with 100 as image width
        double height; // [0,200]
        double angle; // in degrees
        double roundness; // [0,100] (0 = rectangle, 100 = ellipse)

        Rectangle();
        Type getType() const override { return Type::RECTANGLE; };
        bool operator==(const Shape &other) const override;
        bool operator!=(const Shape &other) const override;
        std::unique_ptr<Shape> clone() const override;
    };
    
    struct Polygon: public Shape {
        struct Knot {
            double x; // [-200,200], with 0 as center of the image, 100 = half width of the image
                      //             200 as a limit means that knot can be out of the image
                      //             up to twice the image size
            double y; // [-200,200]
            double roundness; // [0,100] (0 = sharp corner, 100 = entirely round corner)

            Knot();
            void setPos(CoordD &pos);
            bool operator==(const Knot &other) const;
            bool operator!=(const Knot &other) const;
        };
        std::vector<Knot> knots;

        // Convert the Polygon object into a drawable polygon (for rtengine and rtgui)
        static std::vector<CoordD> get_tessellation(std::vector<Knot> &knots);
        void knots_to_list(std::vector<double> &out) const;
        void knots_from_list(const std::vector<double> &v);

        Type getType() const override { return Type::POLYGON; };
        bool operator==(const Shape &other) const override;
        bool operator!=(const Shape &other) const override;
        std::unique_ptr<Shape> clone() const override;
    };

    struct Gradient: public Shape {
        double x;             // [-100; 100] Percentage of image's width
        double y;             // [-100; 100] Percentage of image's height
        double strengthStart; // [0; 100] Strenght of the mask at the transition's begin
        double strengthEnd;   // [0; 100] Strenght of the mask at the transition's end
        double angle;         // [0; 360[ Angle of the gradient

        Gradient();
        Type getType() const override { return Type::GRADIENT; };
        bool operator==(const Shape &other) const override;
        bool operator!=(const Shape &other) const override;
        std::unique_ptr<Shape> clone() const override;
    };

    bool enabled;
    double feather; // [0,100]
    double blur;
    std::vector<double> contrast; // curve
    std::vector<std::unique_ptr<Shape>> shapes;

    AreaMask();
    AreaMask(const AreaMask &other);
    AreaMask &operator=(const AreaMask &other);
    
    bool operator==(const AreaMask &other) const;
    bool operator!=(const AreaMask &other) const;
    bool isTrivial() const;
};

class DeltaEMask {
public:
    bool enabled;
    double L;
    double C;
    double H;
    double range;
    double decay;
    int strength;
    int weight_L;
    int weight_C;
    int weight_H;

    DeltaEMask();
    bool operator==(const DeltaEMask &other) const;
    bool operator!=(const DeltaEMask &other) const;
};


struct DrawnMask {
    struct Stroke {
        double x; // [0,1], with 0 as leftmost point of the image
        double y; // [0,1]
        double radius; // [0,1], with 1 as 10% of the image smallest dimension
        double opacity; // [0,1] with 1 as opaque (strongest)
        bool erase;
        Stroke();
        bool operator==(const Stroke &other) const;
        bool operator!=(const Stroke &other) const;
    };
    bool enabled;
    double feather; // [0,100]    
    double opacity; // [0,1] (1 = opaque, 0 = fully transparent)
    double smoothness; // [0,1] (0 = harsh edges, 1 = fully blurred)
    std::vector<double> contrast; // curve
    std::vector<Stroke> strokes;
    enum Mode {
        INTERSECT,
        ADD,
        ADD_BOUNDED
    };
    Mode mode;
        
    DrawnMask();
    bool operator==(const DrawnMask &other) const;
    bool operator!=(const DrawnMask &other) const;
    bool isTrivial() const;

    void strokes_to_list(std::vector<double> &out) const;
    void strokes_from_list(const std::vector<double> &v);
};


class ParametricMask {
public:
    bool enabled;
    double blur;
    std::vector<double> hue;
    std::vector<double> chromaticity;
    std::vector<double> lightness;
    int lightnessDetail;
    int contrastThreshold;

    ParametricMask();
    bool operator==(const ParametricMask &other) const;
    bool operator!=(const ParametricMask &other) const;
};


class LinkedMask {
public:
    bool enabled;
    bool inverted;
    Glib::ustring toolname;
    Glib::ustring name;

    LinkedMask();
    bool operator==(const LinkedMask &other) const;
    bool operator!=(const LinkedMask &other) const;
};


class ExternalMask {
public:
    bool enabled;
    bool inverted;
    Glib::ustring filename;
    double feather;

    ExternalMask();
    bool operator==(const ExternalMask &other) const;
    bool operator!=(const ExternalMask &other) const;
};


class Mask {
public:
    bool enabled;
    bool inverted;
    ParametricMask parametricMask;
    AreaMask areaMask;
    DeltaEMask deltaEMask;
    DrawnMask drawnMask;
    ExternalMask externalMask;
    LinkedMask linkedMask;
    Glib::ustring name;
    std::vector<double> curve;
    int posterization;
    int smoothing;
    int opacity;

    Mask();
    bool operator==(const Mask &other) const;
    bool operator!=(const Mask &other) const;

    bool load(int ppVersion, const KeyFile &keyfile,
              const Glib::ustring &basedir, const Glib::ustring &group_name,
              const Glib::ustring &prefix, const Glib::ustring &suffix);
    void save(KeyFile &keyfile, const Glib::ustring &basedir,
              const Glib::ustring &group_name,
              const Glib::ustring &prefix, const Glib::ustring &suffix) const;
};


class MaskableParams {
public:
    virtual ~MaskableParams() = default;
    virtual std::string get_name() const = 0;
    virtual std::string get_gui_label() const = 0;
    virtual const std::vector<Mask> &get_masks() const = 0;
};


template<typename T>
class Threshold final {
public:
    Threshold(T _bottom, T _top, bool _start_at_one) :
        Threshold(_bottom, _top, 0, 0, _start_at_one, false)
    {
    }

    Threshold(T _bottom_left, T _top_left, T _bottom_right, T _top_right, bool _start_at_one) :
        Threshold(_bottom_left, _top_left, _bottom_right, _top_right, _start_at_one, true)
    {
    }

    template<typename U = T>
    typename std::enable_if<std::is_floating_point<U>::value, bool>::type operator ==(const Threshold<U>& rhs) const
    {
        if (is_double) {
            return
                std::fabs(bottom_left - rhs.bottom_left) < 1e-10
                && std::fabs(top_left - rhs.top_left) < 1e-10
                && std::fabs(bottom_right - rhs.bottom_right) < 1e-10
                && std::fabs(top_right - rhs.top_right) < 1e-10;
        } else {
            return
                std::fabs(bottom_left - rhs.bottom_left) < 1e-10
                && std::fabs(top_left - rhs.top_left) < 1e-10;
        }
    }

    template<typename U = T>
    typename std::enable_if<std::is_integral<U>::value, bool>::type operator ==(const Threshold<U>& rhs) const
    {
        if (is_double) {
            return
                bottom_left == rhs.bottom_left
                && top_left == rhs.top_left
                && bottom_right == rhs.bottom_right
                && top_right == rhs.top_right;
        } else {
            return
                bottom_left == rhs.bottom_left
                && top_left == rhs.top_left;
        }
    }

    T getBottom() const
    {
        return bottom_left;
    }

    T getTop() const
    {
        return top_left;
    }

    T getBottomLeft() const
    {
        return bottom_left;
    }

    T getTopLeft() const
    {
        return top_left;
    }

    T getBottomRight() const
    {
        return bottom_right;
    }

    T getTopRight() const
    {
        return top_right;
    }

    void setValues(T bottom, T top)
    {
        bottom_left = bottom;
        top_left = top;
    }

    void setValues(T bottom_left, T top_left, T bottom_right, T top_right)
    {
        this->bottom_left = bottom_left;
        this->top_left = top_left;
        this->bottom_right = bottom_right;
        this->top_right = top_right;
    }

    bool isDouble() const
    {
        return is_double;
    }

    std::vector<T> toVector() const
    {
        if (is_double) {
            return {
                bottom_left,
                top_left,
                bottom_right,
                top_right
            };
        } else {
            return {
                bottom_left,
                top_left
            };
        }
    }

    // RT: Type of the returned value
    // RV: Type of the value on the X axis
    // RV2: Type of the maximum value on the Y axis
    template <typename RT, typename RV, typename RV2>
    RT multiply(RV x, RV2 y_max) const
    {
        const double val = x;

        if (init_eql) {
            if (is_double) {
                if (val == static_cast<double>(bottom_right) && static_cast<double>(bottom_right) == static_cast<double>(top_right)) {
                    // This handles the special case where the 2 right values are the same, then bottom one is sent back,
                    // useful if one wants to keep the bottom value even beyond the x max bound
                    return 0;
                }

                if (val >= static_cast<double>(top_right)) {
                    return y_max;
                }

                if (val > static_cast<double>(bottom_right)) {
                    return static_cast<double>(y_max * (val - static_cast<double>(bottom_right)) / (static_cast<double>(top_right) - static_cast<double>(bottom_right)));
                }
            }

            if (val >= static_cast<double>(bottom_left)) {
                return 0;
            }

            if (val > static_cast<double>(top_left)) {
                return static_cast<double>(y_max * (1. - (val - static_cast<double>(bottom_left)) / (static_cast<double>(top_left) - static_cast<double>(bottom_left))));
            }

            return y_max;
        } else {
            if (is_double) {
                if (val == static_cast<double>(bottom_right) && static_cast<double>(bottom_right) == static_cast<double>(top_right)) {
                    // This handles the special case where the 2 right values are the same, then top one is sent back,
                    // useful if one wants to keep the top value even beyond the x max bound
                    return y_max;
                }

                if (val >= static_cast<double>(bottom_right)) {
                    return 0;
                }

                if (val > static_cast<double>(top_right)) {
                    return static_cast<double>(y_max * (1.0 - (val - static_cast<double>(top_right)) / (static_cast<double>(bottom_right) - static_cast<double>(top_right))));
                }
            }

            if (val >= static_cast<double>(top_left)) {
                return y_max;
            }

            if (val > static_cast<double>(bottom_left)) {
                return static_cast<double>(y_max * (val - static_cast<double>(bottom_left)) / (static_cast<double>(top_left) - static_cast<double>(bottom_left)));
            }

            return 0;
        }
    }

private:
    Threshold(T _bottom_left, T _top_left, T _bottom_right, T _top_right, bool _start_at_one, bool _is_double) :
        bottom_left(_bottom_left),
        top_left(_top_left),
        bottom_right(_bottom_right),
        top_right(_top_right),
        init_eql(_start_at_one),
        is_double(_is_double)
    {
    }

    T bottom_left;
    T top_left;
    T bottom_right;
    T top_right;
    bool init_eql;
    bool is_double;
};


struct ExposureParams {
    bool enabled;
    enum HighlightReconstruction {
        HR_OFF,
        HR_BLEND,
        HR_COLOR,
        HR_COLORSOFT
    };
    HighlightReconstruction hrmode;
    double expcomp;
    double black;
    int hrblur;

    ExposureParams();

    bool operator==(const ExposureParams &other) const;
    bool operator!=(const ExposureParams &other) const;
};


struct SaturationParams {
    bool enabled;
    int saturation;
    int vibrance;

    SaturationParams();

    bool operator ==(const SaturationParams &other) const;
    bool operator !=(const SaturationParams &other) const;
};

/**
  * Parameters of the tone curve
  */
struct ToneCurveParams {
    bool enabled;
    
    enum class TcMode {
        STD,               // Standard modes, the curve is applied on all component individually
        WEIGHTEDSTD,       // Weighted standard mode
        FILMLIKE,          // Film-like mode, as defined in Adobe's reference code
        SATANDVALBLENDING, // Modify the Saturation and Value channel
        LUMINANCE,         // Modify the Luminance channel with coefficients from Rec 709's
        PERCEPTUAL,        // Keep color appearance constant using perceptual modeling
        NEUTRAL            // Neutral mode: Standard with JzAzBz hue preservation and ACES-inspired desaturation "sweetener"
    };

    enum class BcMode {
        LINEAR,
        ROLLOFF,
        SCURVE
    };

    int contrast;
    std::vector<double> curve;
    std::vector<double> curve2;
    TcMode curveMode;
    TcMode curveMode2;
    bool histmatching; // histogram matching
    bool fromHistMatching;
    std::vector<double> saturation;
    std::vector<double> saturation2;
    int perceptualStrength;
    bool contrastLegacyMode;
    double whitePoint;
    BcMode basecurve;

    ToneCurveParams();

    bool operator==(const ToneCurveParams &other) const;
    bool operator!=(const ToneCurveParams &other) const;

    bool hasWhitePoint() const;
};


/**
  * Parameters of the luminance curve
  */
struct LabCurveParams {
    bool enabled;
    int brightness;
    int contrast;
    int chromaticity;
    std::vector<double> lcurve;
    std::vector<double> acurve;
    std::vector<double> bcurve;

    LabCurveParams();

    bool operator ==(const LabCurveParams& other) const;
    bool operator !=(const LabCurveParams& other) const;
};


/**
 * Parameters for local contrast
 */
struct LocalContrastParams: public MaskableParams {
    struct Region {
        double contrast;
        std::vector<double> curve;

        Region();
        bool operator==(const Region &other) const;
        bool operator!=(const Region &other) const;
    };

    bool enabled;
    std::vector<Region> regions;
    std::vector<Mask> masks;
    int showMask;
    int selectedRegion;

    LocalContrastParams();

    bool operator==(const LocalContrastParams &other) const;
    bool operator!=(const LocalContrastParams &other) const;

    std::string get_name() const override { return "localcontrast"; }
    std::string get_gui_label() const override { return "TP_LOCALCONTRAST_LABEL"; }
    const std::vector<Mask> &get_masks() const override { return masks; }
};


/**
  * Parameters of the RGB curves
  */
struct RGBCurvesParams {
    bool enabled;
    std::vector<double>   rcurve;
    std::vector<double>   gcurve;
    std::vector<double>   bcurve;

    RGBCurvesParams();

    bool operator ==(const RGBCurvesParams& other) const;
    bool operator !=(const RGBCurvesParams& other) const;
};


/**
  * Parameters of the sharpening
  */
struct SharpeningParams {
    bool           enabled;
    double         contrast;
    double         radius;
    int            amount;
    Threshold<int> threshold;
    bool           edgesonly;
    double         edges_radius;
    int            edges_tolerance;
    bool           halocontrol;
    int            halocontrol_amount;
    Glib::ustring  method;
    int            deconvamount;
    double         deconvradius;
    bool deconvAutoRadius;
    double deconvCornerBoost;
    int deconvCornerLatitude;
    Glib::ustring psf_kernel;
    double psf_iterations;

    SharpeningParams();

    bool operator ==(const SharpeningParams& other) const;
    bool operator !=(const SharpeningParams& other) const;
};


struct WBParams {
    bool enabled;
    enum Type {
        CAMERA,
        AUTO,
        CUSTOM_TEMP,
        CUSTOM_MULT,
        CUSTOM_MULT_LEGACY
    };
    Type method;
    int temperature;
    double green;
    double equal;
    std::array<double, 3> mult;

    WBParams();

    bool operator==(const WBParams &other) const;
    bool operator!=(const WBParams &other) const;
};


/**
 * Parameters of defringing
 */
struct DefringeParams {
    bool    enabled;
    double  radius;
    int     threshold;
    std::vector<double> huecurve;

    DefringeParams();

    bool operator ==(const DefringeParams& other) const;
    bool operator !=(const DefringeParams& other) const;
};

/**
  * Parameters of impulse denoising
  */
struct ImpulseDenoiseParams {
    bool    enabled;
    int     thresh;

    ImpulseDenoiseParams();

    bool operator ==(const ImpulseDenoiseParams& other) const;
    bool operator !=(const ImpulseDenoiseParams& other) const;
};

/**
 * Parameters of the directional pyramid denoising
 */
struct DenoiseParams {
    enum class ChrominanceMethod {
        MANUAL,
        AUTOMATIC
    };

    enum class ColorSpace {
        RGB,
        LAB
    };
    
    bool enabled;
    ColorSpace colorSpace;

    bool aggressive;
    double gamma;

    double luminance;
    double luminanceDetail;
    int luminanceDetailThreshold;

    ChrominanceMethod chrominanceMethod;
    double chrominanceAutoFactor;
    double chrominance;
    double chrominanceRedGreen;
    double chrominanceBlueYellow;

    bool smoothingEnabled;
    int guidedChromaRadius;
    int nlDetail;
    int nlStrength;

    DenoiseParams();

    bool operator ==(const DenoiseParams& other) const;
    bool operator !=(const DenoiseParams& other) const;
};


struct TextureBoostParams: public MaskableParams {
    struct Region {
        double strength;
        double detailThreshold;
        int iterations;

        Region();
        bool operator==(const Region &other) const;
        bool operator!=(const Region &other) const;
    };

    bool enabled;
    std::vector<Region> regions;
    std::vector<Mask> masks;
    int showMask;
    int selectedRegion;

    TextureBoostParams();

    bool operator ==(const TextureBoostParams& other) const;
    bool operator !=(const TextureBoostParams& other) const;

    std::string get_name() const override { return "textureboost"; }
    std::string get_gui_label() const override { return "TP_EPD_LABEL"; }
    const std::vector<Mask> &get_masks() const override { return masks; }
};


struct LogEncodingParams {
    bool enabled;
    bool autocompute;
    bool autogain;
    double gain;
    double targetGray;
    double blackEv;
    double whiteEv;
    int regularization;
    bool satcontrol;
    int highlightCompression;

    LogEncodingParams();

    bool operator==(const LogEncodingParams &other) const;
    bool operator !=(const LogEncodingParams &other) const;
};


struct FattalToneMappingParams {
    bool enabled;
    int threshold;
    int amount;
    bool satcontrol;

    FattalToneMappingParams();

    bool operator ==(const FattalToneMappingParams& other) const;
    bool operator !=(const FattalToneMappingParams& other) const;
};


struct ToneEqualizerParams {
    bool enabled;
    std::array<int, 5> bands;
    int regularization;
    bool show_colormap;
    double pivot;

    ToneEqualizerParams();

    bool operator==(const ToneEqualizerParams &other) const;
    bool operator!=(const ToneEqualizerParams &other) const;
};

/**
  * Parameters of the cropping
  */
struct CropParams {
    bool    enabled;
    int     x;
    int     y;
    int     w;
    int     h;
    bool    fixratio;
    Glib::ustring   ratio;
    Glib::ustring   orientation;
    Glib::ustring   guide;

    CropParams();

    bool operator ==(const CropParams& other) const;
    bool operator !=(const CropParams& other) const;

    void mapToResized(int resizedWidth, int resizedHeight, int scale, int& x1, int& x2, int& y1, int& y2) const;
};

/**
  * Parameters of the coarse transformations like 90 deg rotations and h/v flipping
  */
struct CoarseTransformParams {
    int     rotate;
    bool    hflip;
    bool    vflip;

    CoarseTransformParams();

    bool operator ==(const CoarseTransformParams& other) const;
    bool operator !=(const CoarseTransformParams& other) const;
};

/**
  * Common transformation parameters
  */
struct CommonTransformParams {
    bool autofill;

    CommonTransformParams();

    bool operator ==(const CommonTransformParams& other) const;
    bool operator !=(const CommonTransformParams& other) const;
};

/**
  * Parameters of the rotation
  */
struct RotateParams {
    bool enabled;
    double  degree;

    RotateParams();

    bool operator ==(const RotateParams& other) const;
    bool operator !=(const RotateParams& other) const;
};

/**
  * Parameters of the distortion correction
  */
struct DistortionParams {
    bool enabled;
    double amount;
    bool autocompute;

    DistortionParams();

    bool operator ==(const DistortionParams& other) const;
    bool operator !=(const DistortionParams& other) const;
};

// Lens profile correction parameters
struct LensProfParams {
    enum class LcMode {
        NONE,               // No lens correction
        LENSFUNAUTOMATCH,   // Lens correction using auto matched lensfun database entry
        LENSFUNMANUAL,      // Lens correction using manually selected lensfun database entry
        LCP,                // Lens correction using lcp file
        EXIF                // Lens correction from EXIF metadata
    };

    LcMode lcMode;
    Glib::ustring lcpFile;
    bool useDist, useVign, useCA;
    Glib::ustring lfCameraMake;
    Glib::ustring lfCameraModel;
    Glib::ustring lfLens;

    LensProfParams();

    bool operator ==(const LensProfParams& other) const;
    bool operator !=(const LensProfParams& other) const;

    bool useLensfun() const;
    bool lfAutoMatch() const;
    bool useLcp() const;
    bool lfManual() const;
    bool useExif() const;
    bool needed() const;

    const std::vector<const char*>& getMethodStrings() const;
    Glib::ustring getMethodString(LcMode mode) const;
    LcMode getMethodNumber(const Glib::ustring& mode) const;
};


/**
  * Parameters of the perspective correction
  */
struct PerspectiveParams {
    bool enabled;
    double horizontal;
    double vertical;
    double angle;
    double shear;
    double flength;
    double cropfactor;
    double aspect;
    std::vector<int> control_lines;

    PerspectiveParams();

    bool operator ==(const PerspectiveParams& other) const;
    bool operator !=(const PerspectiveParams& other) const;
};

/**
  * Parameters of the gradient filter
  */
struct GradientParams {
    bool   enabled;
    double degree;
    int    feather;
    double strength;
    int    centerX;
    int    centerY;

    GradientParams();

    bool operator ==(const GradientParams& other) const;
    bool operator !=(const GradientParams& other) const;
};

/**
  * Parameters of the post-crop vignette filter
  */
struct PCVignetteParams {
    bool   enabled;
    double strength;
    int    feather;
    int    roundness;
    int centerX;
    int centerY;

    PCVignetteParams();

    bool operator ==(const PCVignetteParams& other) const;
    bool operator !=(const PCVignetteParams& other) const;
};

/**
  * Parameters of the vignetting correction
  */
struct VignettingParams {
    bool enabled;
    int  amount;
    int  radius;
    int  strength;
    int  centerX;
    int  centerY;

    VignettingParams();

    bool operator ==(const VignettingParams& other) const;
    bool operator !=(const VignettingParams& other) const;
};

/**
  * Parameters of the color mixer
  */
struct ChannelMixerParams {
    bool enabled;
    enum Mode {
        RGB_MATRIX,
        PRIMARIES_CHROMA
    };
    Mode mode;
    
    int red[3];
    int green[3];
    int blue[3];

    int hue_tweak[3];
    int sat_tweak[3];

    ChannelMixerParams();

    bool operator ==(const ChannelMixerParams& other) const;
    bool operator !=(const ChannelMixerParams& other) const;
};

struct BlackWhiteParams {
    bool enabled;

    Glib::ustring filter;
    Glib::ustring setting;
    int mixerRed;
    int mixerGreen;
    int mixerBlue;
    int gammaRed;
    int gammaGreen;
    int gammaBlue;
    Threshold<int> colorCast;

    BlackWhiteParams();

    bool operator ==(const BlackWhiteParams& other) const;
    bool operator !=(const BlackWhiteParams& other) const;
};

struct HSLEqualizerParams {
    bool enabled;
    std::vector<double> hCurve;
    std::vector<double> sCurve;
    std::vector<double> lCurve;
    int smoothing;

    HSLEqualizerParams();

    bool operator==(const HSLEqualizerParams &other) const;
    bool operator!=(const HSLEqualizerParams &other) const;
};

/**
  * Parameters of the c/a correction
  */
struct CACorrParams {
    bool enabled;
    double red;
    double blue;

    CACorrParams();

    bool operator ==(const CACorrParams& other) const;
    bool operator !=(const CACorrParams& other) const;
};

/**
  * Parameters of the resizing
  */
struct ResizeParams {
    bool enabled;
    double scale;
    Glib::ustring appliesTo;
    int dataspec;
    double width;
    double height;
    bool allowUpscaling;
    int ppi;
    enum Unit {
        PX,
        CM,
        INCHES
    };
    Unit unit;

    ResizeParams();

    bool operator ==(const ResizeParams& other) const;
    bool operator !=(const ResizeParams& other) const;

    int get_width() const;
    int get_height() const;
};


/**
  * Parameters entry
  */
struct SpotEntry {
    Coord sourcePos;
    Coord targetPos;
    int radius;
    float feather;
    float opacity;
    int detail;

    SpotEntry();
    float getFeatherRadius() const;

    bool operator ==(const SpotEntry& other) const;
    bool operator !=(const SpotEntry& other) const;
};

/**
  * Parameters of the dust removal tool
  */
struct SpotParams {
    bool enabled;
    std::vector<SpotEntry> entries;

    // the following constant can be used for experimentation before the final merge
    static const short minRadius;
    static const short maxRadius;

    SpotParams();

    bool operator ==(const SpotParams& other) const;
    bool operator !=(const SpotParams& other) const;
};

/**
  * Parameters of the color spaces used during the processing
  */
struct ColorManagementParams {
    Glib::ustring inputProfile;
    bool toneCurve;
    bool applyLookTable;
    bool applyBaselineExposureOffset;
    bool applyHueSatMap;
    int dcpIlluminant;
    bool dcp_look_early;

    Glib::ustring workingProfile;

    Glib::ustring outputProfile;
    RenderingIntent outputIntent;
    bool outputBPC;
    bool inputProfileCAT;

    static const Glib::ustring NoICMString;
    static const Glib::ustring NoProfileString;

    ColorManagementParams();

    bool operator ==(const ColorManagementParams& other) const;
    bool operator !=(const ColorManagementParams& other) const;
};


/**
  * Parameters for metadata handling
  */

typedef std::map<Glib::ustring, Glib::ustring> ExifPairs;
typedef std::map<Glib::ustring, std::vector<Glib::ustring>> IPTCPairs;

struct MetaDataParams {
    enum Mode {
        TUNNEL,
        EDIT,
        STRIP
    };
    Mode mode;
    std::vector<std::string> exifKeys;
    ExifPairs exif;
    IPTCPairs iptc;
    Glib::ustring notes;

    MetaDataParams();

    bool operator==(const MetaDataParams &other) const;
    bool operator!=(const MetaDataParams &other) const;

    static std::vector<std::string> basicExifKeys;
};


/**
 *  Film simualtion params
 */
struct FilmSimulationParams {
    bool enabled;
    Glib::ustring clutFilename;
    int strength;
    bool after_tone_curve;
    CLUTParamValueMap lut_params;

    FilmSimulationParams();

    bool operator ==(const FilmSimulationParams& other) const;
    bool operator !=(const FilmSimulationParams& other) const;
};


struct SoftLightParams {
    bool enabled;
    int strength;

    SoftLightParams();

    bool operator==(const SoftLightParams &other) const;
    bool operator!=(const SoftLightParams &other) const;
};


struct DehazeParams {
    bool enabled;
    std::vector<double> strength;
    bool showDepthMap;
    int depth;
    bool luminance;
    int blackpoint;

    DehazeParams();

    bool operator==(const DehazeParams &other) const;
    bool operator!=(const DehazeParams &other) const;
};


struct GrainParams {
    bool enabled;
    int iso;
    int strength;
    bool color;

    GrainParams();

    bool operator==(const GrainParams &other) const;
    bool operator!=(const GrainParams &other) const;
};


struct SmoothingParams: public MaskableParams {
    struct Region {
        enum class Channel {
            LUMINANCE,
            CHROMINANCE,
            RGB
        };
        enum class Mode {
            GUIDED,
            GAUSSIAN,
            GAUSSIAN_GLOW,
            NLMEANS,
            MOTION,
            LENS,
            NOISE,
            HALATION,
            WAVELETS
        };
        Mode mode;
        Channel channel;
        int radius;
        double sigma;
        int epsilon;
        int iterations;
        double falloff;
        int nldetail;
        int nlstrength;
        int numblades;
        double angle;
        double curvature;
        double offset;
        int noise_strength;
        int noise_coarseness;
        double halation_size;
        double halation_color;
        double wav_strength;
        int wav_levels;
        double wav_gamma;

        Region();
        bool operator==(const Region &other) const;
        bool operator!=(const Region &other) const;
    };
    bool enabled;
    std::vector<Region> regions;
    std::vector<Mask> masks;
    int showMask;
    int selectedRegion;

    SmoothingParams();

    bool operator==(const SmoothingParams &other) const;
    bool operator!=(const SmoothingParams &other) const;

    std::string get_name() const override { return "smoothing"; }
    std::string get_gui_label() const override { return "TP_SMOOTHING_LABEL"; }
    const std::vector<Mask> &get_masks() const override { return masks; }
    
};


struct ColorCorrectionParams: public MaskableParams {
    enum class Mode {
        YUV,
        RGB,
        HSL,
        JZAZBZ,
        LUT
    };
    struct Region {
        double a;
        double b;
        double abscale;
        double inSaturation;
        double outSaturation;
        std::array<double, 3> slope;
        std::array<double, 3> offset;
        std::array<double, 3> power;
        std::array<double, 3> pivot;
        std::array<double, 3> hue;
        std::array<double, 3> sat;
        std::array<double, 3> factor;
        std::array<double, 3> compression;
        bool rgbluminance;
        double hueshift;
        Glib::ustring lutFilename;
        CLUTParamValueMap lut_params;
        double hsl_gamma;
        Mode mode;

        Region();
        bool operator==(const Region &other) const;
        bool operator!=(const Region &other) const;
    };

    bool enabled;
    std::vector<Region> regions;
    std::vector<Mask> masks;
    int showMask;
    int selectedRegion;

    ColorCorrectionParams();
    bool operator==(const ColorCorrectionParams &other) const;
    bool operator!=(const ColorCorrectionParams &other) const;

    std::string get_name() const override { return "colorcorrection"; }
    std::string get_gui_label() const override { return "TP_COLORCORRECTION_LABEL"; }
    const std::vector<Mask> &get_masks() const override { return masks; }
};


/**
  * Parameters for RAW demosaicing, common to all sensor type
  */
struct RAWParams {
    /**
     * Parameters for RAW demosaicing specific to Bayer sensors
     */
    struct BayerSensor {
        enum class Method {
            AMAZE,
            RCD,
            LMMSE,
            IGV,
            AMAZEBILINEAR,
            RCDBILINEAR,
            VNG4,
            FAST,
            MONO,
            PIXELSHIFT,
            NONE,
            AMAZEVNG4,
            RCDVNG4,
            DCB,
            DCBBILINEAR,
            DCBVNG4,
            AHD,
            EAHD,
            HPHD
        };

        enum class PSMotionCorrectionMethod {
            OFF,
            AUTO,
            CUSTOM
        };

        enum class PSDemosaicMethod {
            AMAZE,
            AMAZEVNG4,
            LMMSE
        };

        Method method;
        int border;
        int imageNum;
        int ccSteps;
        double black0;
        double black1;
        double black2;
        double black3;
        bool twogreen;
        int linenoise;
        enum class LineNoiseDirection {
            HORIZONTAL = 1,
            VERTICAL,
            BOTH,
            PDAF_LINES = 5
        };
        LineNoiseDirection linenoiseDirection;
        int greenthresh;
        int dcb_iterations;
        int lmmse_iterations;
        bool dualDemosaicAutoContrast;
        double dualDemosaicContrast;
        PSMotionCorrectionMethod pixelShiftMotionCorrectionMethod;
        double pixelShiftEperIso;
        double pixelShiftSigma;
        bool pixelShiftShowMotion;
        bool pixelShiftShowMotionMaskOnly;
        bool pixelShiftHoleFill;
        bool pixelShiftMedian;
        bool pixelShiftGreen;
        bool pixelShiftBlur;
        double pixelShiftSmoothFactor;
        bool pixelShiftEqualBright;
        bool pixelShiftEqualBrightChannel;
        bool pixelShiftNonGreenCross;
        Glib::ustring pixelShiftDemosaicMethod;
        bool dcb_enhance;
        bool pdafLinesFilter;
        bool dynamicRowNoiseFilter;

        // some enable flags
        bool enable_black;
        bool enable_preproc;

        BayerSensor();

        bool operator ==(const BayerSensor& other) const;
        bool operator !=(const BayerSensor& other) const;

        void setPixelShiftDefaults();

        static const std::vector<const char*>& getMethodStrings();
        static Glib::ustring getMethodString(Method method);

        static const std::vector<const char*>& getPSDemosaicMethodStrings();
        static Glib::ustring getPSDemosaicMethodString(PSDemosaicMethod method);
    };

    /**
     * Parameters for RAW demosaicing specific to X-Trans sensors
     */
    struct XTransSensor {
        enum class Method {
            FOUR_PASS,
            THREE_PASS,
            TWO_PASS,
            ONE_PASS,
            FAST,
            MONO,
            NONE
        };

        Method method;
        bool dualDemosaicAutoContrast;
        double dualDemosaicContrast;
        int border;
        int ccSteps;
        double blackred;
        double blackgreen;
        double blackblue;

        bool enable_black;

        XTransSensor();

        bool operator ==(const XTransSensor& other) const;
        bool operator !=(const XTransSensor& other) const;

        static const std::vector<const char*>& getMethodStrings();
        static Glib::ustring getMethodString(Method method);
    };

    BayerSensor bayersensor;         ///< RAW parameters for Bayer sensors
    XTransSensor xtranssensor;       ///< RAW parameters for X-Trans sensors

    enum class FlatFieldBlurType {
        AREA,
        V,
        H,
        VH,
    };

    Glib::ustring dark_frame;
    bool df_autoselect;

    Glib::ustring ff_file;
    bool ff_AutoSelect;
    int ff_BlurRadius;
    Glib::ustring ff_BlurType;
    bool ff_AutoClipControl;
    int ff_clipControl;
    bool ff_embedded;

    bool ca_autocorrect;
    bool ca_avoidcolourshift;
    int caautoiterations;
    double cared;
    double cablue;

    // exposure before interpolation
    double expos;

    bool hotPixelFilter;
    bool deadPixelFilter;
    int hotdeadpix_thresh;

    // some enable flags
    bool enable_darkframe;
    bool enable_flatfield;
    bool enable_ca;
    bool enable_hotdeadpix;
    bool enable_whitepoint;

    RAWParams();

    bool operator ==(const RAWParams& other) const;
    bool operator !=(const RAWParams& other) const;

    static const std::vector<const char*>& getFlatFieldBlurTypeStrings();
    static Glib::ustring getFlatFieldBlurTypeString(FlatFieldBlurType type);
};


/**
  * Parameters of film negative
  */
struct FilmNegativeParams {
    bool enabled;
    double redRatio;
    double greenExp;
    double blueRatio;

    struct RGB {
        float r, g, b;

        bool operator ==(const RGB& other) const;
        bool operator !=(const RGB& other) const;
        RGB operator *(const RGB& other) const;
    };

    RGB refInput;
    RGB refOutput;

    enum class ColorSpace {
        INPUT = 0,
        WORKING
        // TODO : add support for custom color profile
    };

    ColorSpace colorSpace;

    enum class BackCompat { CURRENT = 0, V1, V2 };
    BackCompat backCompat;

    FilmNegativeParams();

    bool operator ==(const FilmNegativeParams& other) const;
    bool operator !=(const FilmNegativeParams& other) const;
};


/**
  * This class holds all the processing parameters applied on the images
  */
class ProcParams {
public:
    ExposureParams          exposure;
    SaturationParams saturation;
    ToneCurveParams         toneCurve;       ///< Tone curve parameters
    LabCurveParams          labCurve;        ///< CIELAB luminance curve parameters
    LocalContrastParams     localContrast;   ////< Local contrast parameters
    RGBCurvesParams         rgbCurves;       ///< RGB curves parameters
    SharpeningParams        sharpening;      ///< Sharpening parameters
    SharpeningParams        prsharpening;    ///< Sharpening parameters for post resize sharpening
    WBParams                wb;              ///< White balance parameters
    DefringeParams          defringe;        ///< Defringing parameters
    ImpulseDenoiseParams    impulseDenoise;  ///< Impulse denoising parameters
    DenoiseParams           denoise;   ///< Directional Pyramid denoising parameters
    TextureBoostParams      textureBoost;  ///< Edge Preserving Decomposition parameters
    FattalToneMappingParams fattal;          ///< Dynamic Range Compression
    LogEncodingParams       logenc;
    ToneEqualizerParams     toneEqualizer;
    CropParams              crop;            ///< Crop parameters
    CoarseTransformParams   coarse;          ///< Coarse transformation (90, 180, 270 deg rotation, h/v flipping) parameters
    CommonTransformParams   commonTrans;     ///< Common transformation parameters (autofill)
    RotateParams            rotate;          ///< Rotation parameters
    DistortionParams        distortion;      ///< Lens distortion correction parameters
    LensProfParams          lensProf;        ///< Lens correction profile parameters
    PerspectiveParams       perspective;     ///< Perspective correction parameters
    GradientParams          gradient;        ///< Gradient filter parameters
    PCVignetteParams        pcvignette;      ///< Post-crop vignette filter parameters
    CACorrParams            cacorrection;    ///< Lens c/a correction parameters
    VignettingParams        vignetting;      ///< Lens vignetting correction parameters
    ChannelMixerParams      chmixer;         ///< Channel mixer parameters
    BlackWhiteParams        blackwhite;      ///< Black&  White parameters
    HSLEqualizerParams      hsl;
    ResizeParams            resize;          ///< Resize parameters
    SpotParams              spot;            ///< Spot removal tool
    ColorManagementParams   icm;             ///< profiles/color spaces used during the image processing
    RAWParams               raw;             ///< RAW parameters before demosaicing
    FilmSimulationParams    filmSimulation;  ///< film simulation parameters
    SoftLightParams         softlight;       ///< softlight parameters
    DehazeParams            dehaze;          ///< dehaze parameters
    GrainParams             grain;
    SmoothingParams         smoothing;
    ColorCorrectionParams   colorcorrection;
    FilmNegativeParams      filmNegative;
    int                     rank;            ///< Custom image quality ranking
    int                     colorlabel;      ///< Custom color label
    bool                    inTrash;         ///< Marks deleted image
    Glib::ustring           appVersion;      ///< Version of the application that generated the parameters
    int                     ppVersion;       ///< Version of the PP file from which the parameters have been read

    MetaDataParams          metadata;        ///< Metadata parameters
    // ExifPairs               exif;            ///< List of modifications appplied on the exif tags of the input image
    // IPTCPairs               iptc;            ///< The IPTC tags and values to be saved to the output image

    /**
      * The constructor only sets the hand-wired defaults.
      */
    ProcParams();
    /**
      * Sets the hand-wired defaults parameters.
      */
    void setDefaults();

    /**
      * Loads the parameters from a file.
      * @param fname the name of the file
      * @params pedited pointer to a ParamsEdited object (optional) to store which values has been loaded
      * @return Error code (=0 if no error)
      */
    int load(ProgressListener *pl,
             const Glib::ustring& fname, const ParamsEdited *pedited=nullptr);

    int load(ProgressListener *pl,
             const KeyFile &keyFile, const ParamsEdited *pedited=nullptr,
             bool resetOnError=true, const Glib::ustring &fname="");
    int save(ProgressListener *pl,
             KeyFile &keyFile, const ParamsEdited *pedited=nullptr,
             const Glib::ustring &fname="") const;
    /**
      * Saves the parameters to possibly two files. This is a performance improvement if a function has to
      * save the same file in two different location, i.e. the cache and the image's directory
      * @param fname   the name of the first file (can be an empty string)
      * @param fname2  the name of the second file (can be an empty string) (optional)
      * @param fnameAbsolute set to false if embedded filenames (if any, darkframe/flatfield) should be stored as relative
      * filenames if they are inside the same directory or in a sub-directory to fname's directory.
      * @param pedited pointer to a ParamsEdited object (optional) to store which values has to be saved
      * @return Error code (=0 if all supplied filenames where created correctly)
      */
    int save(ProgressListener *pl,
             const Glib::ustring &fname, const Glib::ustring &fname2=Glib::ustring(), const ParamsEdited *pedited=nullptr);

    int saveEmbedded(ProgressListener *pl, const Glib::ustring &fname);

    /** Creates a new instance of ProcParams.
      * @return a pointer to the new ProcParams instance. */
    static ProcParams *create();

    /** Destroys an instance of ProcParams.
      * @param pp a pointer to the ProcParams instance to destroy. */
    static void destroy(ProcParams* pp);

    static void init();
    static void cleanup();

    bool operator ==(const ProcParams& other) const;
    bool operator !=(const ProcParams& other) const;

    bool from_data(const char *data);
    std::string to_data() const;

    std::vector<const MaskableParams *> get_maskable() const;

private:
    /** Write the ProcParams's text in the file of the given name.
    * @param fname the name of the file
    * @param content the text to write
    * @return Error code (=0 if no error)
    * */
    int write(ProgressListener *pl,
              const Glib::ustring& fname, const Glib::ustring& content) const;

    int load(ProgressListener *pl,
             bool load_general,
             const KeyFile &keyFile, const ParamsEdited *pedited,
             bool resetOnError, const Glib::ustring &fname);
    int save(ProgressListener *pl,
             bool save_general,
             KeyFile &keyFile, const ParamsEdited *pedited,
             const Glib::ustring &fname) const;

    friend class ProcParamsWithSnapshots;
};


class ProcParamsWithSnapshots {
public:
    int load(ProgressListener *pl, const Glib::ustring &fname);
    int save(ProgressListener *pl, const Glib::ustring &fname, const Glib::ustring &fname2=Glib::ustring());

    ProcParams master;
    std::vector<std::pair<Glib::ustring, ProcParams>> snapshots;
};


class PartialProfile {
public:
    virtual ~PartialProfile() = default;
    virtual bool applyTo(ProcParams &pp) const = 0;
};


class FullPartialProfile: public PartialProfile {
public:
    FullPartialProfile();
    explicit FullPartialProfile(const ProcParams &pp);
    bool applyTo(ProcParams &pp) const override;

private:
    ProcParams pp_;
};


class FilePartialProfile: public PartialProfile {
public:
    FilePartialProfile(): pl_(nullptr), fname_(""), append_(false) {}
    FilePartialProfile(ProgressListener *pl, const Glib::ustring &fname, bool append);
    bool applyTo(ProcParams &pp) const override;
    const Glib::ustring &filename() const { return fname_; }

private:
    ProgressListener *pl_;
    Glib::ustring fname_;
    bool append_;
};


class PEditedPartialProfile: public PartialProfile {
public:
    PEditedPartialProfile(ProgressListener *pl, const Glib::ustring &fname, const ParamsEdited &pe);
    PEditedPartialProfile(const ProcParams &pp, const ParamsEdited &pe);
    bool applyTo(ProcParams &pp) const override;

private:
    ProgressListener *pl_;
    Glib::ustring fname_;
    ProcParams pp_;
    ParamsEdited pe_;
};


class MultiPartialProfile: public PartialProfile {
public:
    MultiPartialProfile() {}
    void add(const PartialProfile *p);
    void clear();
    bool applyTo(ProcParams &pp) const override;
    operator bool() const { return !profiles_.empty(); }

private:
    std::vector<const PartialProfile *> profiles_;
};
    

}} // namespace rtengine::procparams

