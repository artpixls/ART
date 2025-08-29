/*
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
#include "refreshmap.h"
#include "procevents.h"


namespace rtengine {

// Aligned so the first entry starts on line 30.
int refreshmap[NUMOFEVENTS] = {
    ALL,              // EvPhotoLoaded,
    ALL,              // EvProfileLoaded,
    ALL,              // EvProfileChanged,
    ALL,              // EvHistoryBrowsed,
    LUMINANCECURVE,   // EvBrightness,
    LUMINANCECURVE,   // EvContrast,
    RGBCURVE,         // EvBlack,
    ALLNORAW,         // EvExpComp,
    RGBCURVE,         // EvHLCompr,
    RGBCURVE,         // EvSHCompr,
    LUMINANCECURVE,   // EvToneCurve1,
    AUTOEXP,          // EvAutoExp,
    AUTOEXP,          // EvClip,
    LUMINANCECURVE,   // EvLBrightness,
    LUMINANCECURVE,   // EvLContrast,
    LUMINANCECURVE,   // EvLBlack,
    LUMINANCECURVE,   // EvLHLCompr,
    LUMINANCECURVE,   // EvLSHCompr,
    LUMINANCECURVE,   // EvLLCurve,
    SHARPENING,       // EvShrEnabled,
    SHARPENING,       // EvShrRadius,
    SHARPENING,       // EvShrAmount,
    SHARPENING,       // EvShrThresh,
    SHARPENING,       // EvShrEdgeOnly,
    SHARPENING,       // EvShrEdgeRadius,
    SHARPENING,       // EvShrEdgeTolerance,
    SHARPENING,       // EvShrHaloControl,
    SHARPENING,       // EvShrHaloAmount,
    SHARPENING,       // EvShrMethod,
    SHARPENING,       // EvShrDRadius,
    SHARPENING,       // EvShrDAmount,
    SHARPENING,       // EvShrDDamping,
    SHARPENING,       // EvShrDIterations,
    TRANSFORM,        // EvLCPUseDist,
    DARKFRAME,        // EvLCPUseVign,
    TRANSFORM,        // EvLCPUseCA,
    M_VOID,           // EvFixedExp
    WHITEBALANCE,     // EvWBMethod,
    WHITEBALANCE,     // EvWBTemp,
    WHITEBALANCE,     // EvWBGreen,
    LUMINANCECURVE,   // EvToneCurveMode1,
    LUMINANCECURVE,   // EvToneCurve2,
    LUMINANCECURVE,   // EvToneCurveMode2,
    0,                // EvLDNRadius: obsolete,
    0,                // EvLDNEdgeTolerance: obsolete,
    0,                // EvCDNEnabled:obsolete,
    0,                // free entry
    RGBCURVE|M_AUTOEXP, // EvDCPToneCurve,
    ALLNORAW,         // EvDCPIlluminant,
    0,          // EvSHEnabled,
    RGBCURVE,         // EvSHHighlights,
    RGBCURVE,         // EvSHShadows,
    RGBCURVE,         // EvSHHLTonalW,
    RGBCURVE,         // EvSHSHTonalW,
    RGBCURVE,         // EvSHLContrast,
    0,          // EvSHRadius,
    ALLNORAW,         // EvCTRotate,
    ALLNORAW,         // EvCTHFlip,
    ALLNORAW,         // EvCTVFlip,
    TRANSFORM,        // EvROTDegree,
    TRANSFORM,        // EvTransAutoFill,
    TRANSFORM,        // EvDISTAmount,
    ALL,              // EvBookmarkSelected,
    CROP,             // EvCrop,
    TRANSFORM,        // EvCACorr,
    ALLNORAW,         // EvHREnabled,
    ALLNORAW,         // EvHRAmount,
    ALLNORAW|M_RAW,   // EvHRMethod,
    DEMOSAIC,         // EvWProfile,
    LUMINANCECURVE/*OUTPUTPROFILE*/,    // EvOProfile,
    ALLNORAW,         // EvIProfile,
    TRANSFORM,        // EvVignettingAmount,
    RGBCURVE,         // EvChMixer,
    RESIZE,           // EvResizeScale,
    RESIZE,           // EvResizeMethod,
    EXIF,             // EvExif,
    IPTC,             // EvIPTC
    RESIZE,           // EvResizeSpec,
    RESIZE,           // EvResizeWidth
    RESIZE,           // EvResizeHeight
    RESIZE,           // EvResizeEnabled
    ALL,              // EvProfileChangeNotification
    0,          // EvShrHighQuality
    TRANSFORM,        // EvPerspCorr
    DARKFRAME,        // EvLCPFile
    RGBCURVE,         // EvRGBrCurveLumamode
    DETAIL,   // EvIDNEnabled,
    DETAIL,   // EvIDNThresh,
    HDR,         // EvDPDNEnabled,
    HDR,         // EvDPDNLuma,
    HDR,         // EvDPDNChroma,
    HDR,         // EvDPDNGamma,
    DISPLAY,         // EvDirPyrEqualizer,
    DISPLAY,         // EvDirPyrEqlEnabled,
    LUMINANCECURVE,   // EvLSaturation,
    LUMINANCECURVE,   // EvLaCurve,
    LUMINANCECURVE,   // EvLbCurve,
    DEMOSAIC,         // EvDemosaicMethod
    DARKFRAME,        // EvPreProcessHotPixel
    LUMINANCECURVE,   // EvSaturation,
    RGBCURVE,         // EvHSVEqualizerH,
    RGBCURVE,         // EvHSVEqualizerS,
    RGBCURVE,         // EvHSVEqualizerV,
    RGBCURVE,         // EvHSVEqEnabled,
    DETAIL,         // EvDefringeEnabled,
    DETAIL,         // EvDefringeRadius,
    DETAIL,         // EvDefringeThreshold,
    RGBCURVE,         // EvHLComprThreshold,
    RESIZE,           // EvResizeBoundingBox
    RESIZE,           // EvResizeAppliesTo
    LUMINANCECURVE,   // EvCBAvoidClip,
    LUMINANCECURVE,   // EvCBSatLimiter,
    LUMINANCECURVE,   // EvCBSatLimit,
    DEMOSAIC,         // EvDemosaicDCBIter
    ALLNORAW,         // EvDemosaicFalseColorIter
    DEMOSAIC,         // EvDemosaicDCBEnhanced
    DARKFRAME,        // EvPreProcessCARed
    DARKFRAME,        // EvPreProcessCABlue
    DARKFRAME,        // EvPreProcessLineDenoise
    DARKFRAME,        // EvPreProcessGEquilThresh
    DARKFRAME,        // EvPreProcessAutoCA
    DARKFRAME,        // EvPreProcessAutoDF
    DARKFRAME,        // EvPreProcessDFFile
    DARKFRAME,        // EvPreProcessExpCorrLinear
    0,                // --unused--
    FLATFIELD,        // EvFlatFieldFile,
    FLATFIELD,        // EvFlatFieldAutoSelect,
    FLATFIELD,        // EvFlatFieldBlurRadius,
    FLATFIELD,        // EvFlatFieldBlurType,
    TRANSFORM,        // EvAutoDIST,
    HDR,         // EvDPDNLumCurve,
    HDR,         // EvDPDNChromCurve,
    GAMMA,            // EvGAMMA
    GAMMA,            // EvGAMPOS
    GAMMA,            // EvGAMFREE
    GAMMA,            // EvSLPOS
    DARKFRAME,        // EvPreProcessExpBlackzero
    DARKFRAME,        // EvPreProcessExpBlackone
    DARKFRAME,        // EvPreProcessExpBlacktwo
    DARKFRAME,        // EvPreProcessExpBlackthree
    DARKFRAME,        // EvPreProcessExptwoGreen
    SHARPENING,       // EvSharpenEdgePasses
    SHARPENING,       // EvSharpenEdgeStrength
    DIRPYREQUALIZER,  // EvSharpenMicroStrength
    DIRPYREQUALIZER,  // EvSharpenMicroUniformity
    SHARPENING,       // EvSharpenEdgeEnabled
    SHARPENING,       // EvSharpenEdgeThreechannels
    DIRPYREQUALIZER,  // EvSharpenMicroEnabled
    DIRPYREQUALIZER,  // EvSharpenMicroMatrix
    DEMOSAIC,         // EvDemosaicALLEnhanced Disabled but not removed for now, may be reintroduced some day
    RGBCURVE,         // EvVibranceEnabled
    RGBCURVE,         // EvVibrancePastels
    RGBCURVE,         // EvVibranceSaturated
    RGBCURVE,         // EvVibranceProtectSkins
    RGBCURVE,         // EvVibranceAvoidColorShift
    RGBCURVE,         // EvVibrancePastSatTog
    RGBCURVE,         // EvVibrancePastSatThreshold
    DISPLAY,  // EvEPDStrength
    DISPLAY,  // EvEPDEdgeStopping
    DISPLAY,  // EvEPDScale
    DISPLAY,  // EvEPDReweightingIterates
    DISPLAY,  // EvEPDEnabled
    LUMINANCECURVE,         // EvRGBrCurve
    LUMINANCECURVE,         // EvRGBgCurve
    LUMINANCECURVE,         // EvRGBbCurve
    RGBCURVE,         // EvNeutralExp
    DEMOSAIC | M_PREPROC, // EvDemosaicMethodPreProc
    LUMINANCECURVE,   // EvLCCurve
    LUMINANCECURVE,   // EvLCHCurve
    RGBCURVE,         // EvVibranceSkinTonesCurve
    LUMINANCECURVE,   // EvLLCCurve
    LUMINANCECURVE,   // EvLLCredsk
    HDR,         // EvDPDNLdetail
    ALLNORAW,         // EvCATEnabled
    LUMINANCECURVE,   // EvCATDegree
    LUMINANCECURVE,   // EvCATMethodsur
    LUMINANCECURVE,   // EvCATAdapscen
    LUMINANCECURVE,   // EvCATAdapLum
    LUMINANCECURVE,   // EvCATMethodWB
    LUMINANCECURVE,   // EvCATJLight
    LUMINANCECURVE,   // EvCATChroma
    LUMINANCECURVE,   // EvCATAutoDegree
    LUMINANCECURVE,   // EvCATContrast
    LUMINANCECURVE,   // EvCATSurr
    LUMINANCECURVE,   // EvCATgamut
    LUMINANCECURVE,   // EvCATmethodalg
    LUMINANCECURVE,   // EvCATRstpro
    LUMINANCECURVE,   // EvCATQbright
    LUMINANCECURVE,   // EvCATQContrast
    LUMINANCECURVE,   // EvCATSChroma
    LUMINANCECURVE,   // EvCATMchroma
    LUMINANCECURVE,   // EvCAThue
    LUMINANCECURVE,   // EvCATcurve1
    LUMINANCECURVE,   // EvCATcurve2
    LUMINANCECURVE,   // EvCATcurvemode1
    LUMINANCECURVE,   // EvCATcurvemode2
    LUMINANCECURVE,   // EvCATcurve3
    LUMINANCECURVE,   // EvCATcurvemode3
    LUMINANCECURVE,   // EvCATdatacie
    LUMINANCECURVE,   // EvCATtonecie
    HDR,         // EvDPDNbluechro
    HDR,         // EvDPDNperform
    HDR,         // EvDPDNmet
    DEMOSAIC,         // EvDemosaicLMMSEIter
    LUMINANCECURVE,   // EvCATbadpix
    LUMINANCECURVE,   // EvCATAutoadap
    DETAIL,           // EvPFCurve
    WHITEBALANCE,     // EvWBequal
    WHITEBALANCE,     // EvWBequalbo
    LUMINANCECURVE,   // EvGradientDegree
    LUMINANCECURVE,   // EvGradientEnabled
    LUMINANCECURVE,   // EvPCVignetteStrength
    LUMINANCECURVE,   // EvPCVignetteEnabled
    RGBCURVE,         // EvBWChmixEnabled
    RGBCURVE,         // EvBWred
    RGBCURVE,         // EvBWgreen
    RGBCURVE,         // EvBWblue
    RGBCURVE,         // EvBWredgam
    RGBCURVE,         // EvBWgreengam
    RGBCURVE,         // EvBWbluegam
    RGBCURVE,         // EvBWfilter
    RGBCURVE,         // EvBWsetting
    RGBCURVE,         // EvBWoran
    RGBCURVE,         // EvBWyell
    RGBCURVE,         // EvBWcyan
    RGBCURVE,         // EvBWmag
    RGBCURVE,         // EvBpur
    RGBCURVE,         // EvBWLuminanceEqual
    RGBCURVE,         // EvBWChmixEnabledLm
    RGBCURVE,         // EvBWmethod
    RGBCURVE,         // EvBWBeforeCurve
    RGBCURVE,         // EvBWBeforeCurveMode
    RGBCURVE,         // EvBWAfterCurve
    RGBCURVE,         // EvBWAfterCurveMode
    RGBCURVE,         // EvAutoch
    0,                // --unused--
    RGBCURVE,         // EvNeutralBW
    LUMINANCECURVE,   // EvGradientFeather
    LUMINANCECURVE,   // EvGradientStrength
    LUMINANCECURVE,   // EvGradientCenter
    LUMINANCECURVE,   // EvPCVignetteFeather
    LUMINANCECURVE,   // EvPCVignetteRoundness
    TRANSFORM,        // EvVignettingRadius,
    TRANSFORM,        // EvVignettingStrength
    TRANSFORM,        // EvVignettingCenter
    LUMINANCECURVE,   // EvLCLCurve
    LUMINANCECURVE,   // EvLLHCurve
    LUMINANCECURVE,   // EvLHHCurve
    DISPLAY,         // EvDirPyrEqualizerThreshold
    HDR,         // EvDPDNenhance
    RGBCURVE,         // EvBWMethodalg
    ALLNORAW,         // EvDirPyrEqualizerSkin
    ALLNORAW,         // EvDirPyrEqlgamutlab
    ALLNORAW,         // EvDirPyrEqualizerHueskin
    HDR,         // EvDPDNmedian
    HDR,         // EvDPDNmedmet
    RGBCURVE,         // EvColorToningEnabled
    RGBCURVE,         // EvColorToningColor
    RGBCURVE,         // EvColorToningOpacity
    RGBCURVE,         // EvColorToningCLCurve
    RGBCURVE,         // EvColorToningMethod
    RGBCURVE,         // EvColorToningLLCurve
    RGBCURVE,         // EvColorToningredlow
    RGBCURVE,         // EvColorToninggreenlow
    RGBCURVE,         // EvColorToningbluelow
    RGBCURVE,         // EvColorToningredmed
    RGBCURVE,         // EvColorToninggreenmed
    RGBCURVE,         // EvColorToningbluemed
    RGBCURVE,         // EvColorToningredhigh
    RGBCURVE,         // EvColorToninggreenhigh
    RGBCURVE,         // EvColorToningbluehigh
    RGBCURVE,         // EvColorToningbalance
    RGBCURVE,         // EvColorToningNeutral
    RGBCURVE,         // EvColorToningsatlow
    RGBCURVE,         // EvColorToningsathigh
    RGBCURVE,         // EvColorToningTwocolor
    RGBCURVE,         // EvColorToningNeutralcur
    RGBCURVE,         // EvColorToningLumamode
    RGBCURVE,         // EvColorToningShadows
    RGBCURVE,         // EvColorToningHighights
    RGBCURVE,         // EvColorToningSatProtection
    RGBCURVE,         // EvColorToningSatThreshold
    RGBCURVE,         // EvColorToningStrength
    RGBCURVE,         // EvColorToningautosat
    HDR,         // EvDPDNmetmed
    HDR,         // EvDPDNrgbmet
    HDR,         // EvDPDNpasses
    FLATFIELD,        // EvFlatFieldClipControl
    FLATFIELD,        // EvFlatFieldAutoClipControl
    DARKFRAME,        // EvPreProcessExpBlackRed
    DARKFRAME,        // EvPreProcessExpBlackGreen
    DARKFRAME,        // EvPreProcessExpBlackBlue
    RGBCURVE,         // EvFilmSimulationEnabled
    RGBCURVE,         // EvFilmSimulationStrength
    RGBCURVE,         // EvFilmSimulationFilename
    HDR,         // EvDPDNLCurve
    HDR,         // EvDPDNsmet
    DARKFRAME,        // EvPreProcessDeadPixel
    HDR,         // EvDPDNCCCurve
    HDR,         // EvDPDNautochroma
    HDR,         // EvDPDNLmet
    HDR,         // EvDPDNCmet
    HDR,         // EvDPDNC2met
    DIRPYREQUALIZER,  // EvWavelet
    DIRPYREQUALIZER,  // EvEnabled
    DIRPYREQUALIZER,  // EvWavLmethod
    DIRPYREQUALIZER,  // EvWavCLmethod
    DIRPYREQUALIZER,  // EvWavDirmethod
    DIRPYREQUALIZER,  // EvWavtiles
    DIRPYREQUALIZER,  // EvWavsky
    DIRPYREQUALIZER,  // EvWavthres
    DIRPYREQUALIZER,  // EvWavthr
    DIRPYREQUALIZER,  // EvWavchroma
    DIRPYREQUALIZER,  // EvWavmedian
    DIRPYREQUALIZER,  // EvWavunif
    DIRPYREQUALIZER,  // EvWavSkin
    DIRPYREQUALIZER,  // EvWavHueSkin
    DIRPYREQUALIZER,  // EvWavThreshold
    DIRPYREQUALIZER,  // EvWavlhl
    DIRPYREQUALIZER,  // EvWavbhl
    DIRPYREQUALIZER,  // EvWavThresHold2
    DIRPYREQUALIZER,  // EvWavavoid
    DIRPYREQUALIZER,  // EvWavCCCurve
    DIRPYREQUALIZER,  // EvWavpast
    DIRPYREQUALIZER,  // EvWavsat
    DIRPYREQUALIZER,  // EvWavCHmet
    DIRPYREQUALIZER,  // EvWavHSmet
    DIRPYREQUALIZER,  // EvWavchro
    DIRPYREQUALIZER,  // EvWavColor
    DIRPYREQUALIZER,  // EvWavOpac
    DIRPYREQUALIZER,  // EvWavsup
    DIRPYREQUALIZER,  // EvWavTilesmet
    DIRPYREQUALIZER,  // EvWavrescon
    DIRPYREQUALIZER,  // EvWavreschro
    DIRPYREQUALIZER,  // EvWavresconH
    DIRPYREQUALIZER,  // EvWavthrH
    DIRPYREQUALIZER,  // EvWavHueskin2
    DIRPYREQUALIZER,  // EvWavedgrad
    DIRPYREQUALIZER,  // EvWavedgval
    DIRPYREQUALIZER,  // EvWavStrngth
    DIRPYREQUALIZER,  // EvWavdaubcoeffmet
    DIRPYREQUALIZER,  // EvWavedgreinf
    DIRPYREQUALIZER,  // EvWaveletch
    DIRPYREQUALIZER,  // EvWavCHSLmet
    DIRPYREQUALIZER,  // EvWavedgcont
    DIRPYREQUALIZER,  // EvWavEDmet
    DIRPYREQUALIZER,  // EvWavlev0nois
    DIRPYREQUALIZER,  // EvWavlev1nois
    DIRPYREQUALIZER,  // EvWavlev2nois
    DIRPYREQUALIZER,  // EvWavmedianlev
    DIRPYREQUALIZER,  // EvWavHHCurve
    DIRPYREQUALIZER,  // EvWavBackmet
    DIRPYREQUALIZER,  // EvWavedgedetect
    DIRPYREQUALIZER,  // EvWavlipst
    DIRPYREQUALIZER,  // EvWavedgedetectthr
    DIRPYREQUALIZER,  // EvWavedgedetectthr2
    DIRPYREQUALIZER,  // EvWavlinkedg
    DIRPYREQUALIZER,  // EvWavCHCurve
    DARKFRAME,        // EvPreProcessHotDeadThresh
    DISPLAY,       // EvEPDgamma
    DIRPYREQUALIZER,  // EvWavtmr
    DIRPYREQUALIZER,  // EvWavTMmet
    DIRPYREQUALIZER,  // EvWavtmrs
    DIRPYREQUALIZER,  // EvWavbalance
    DIRPYREQUALIZER,  // EvWaviter
    DIRPYREQUALIZER,  // EvWavgamma
    DIRPYREQUALIZER,  // EvWavCLCurve
    DIRPYREQUALIZER,  // EvWavopacity
    DIRPYREQUALIZER,  // EvWavBAmet
    DIRPYREQUALIZER,  // EvWavopacityWL
    M_LUMINANCE,           // EvPrShrEnabled
    M_LUMINANCE,           // EvPrShrRadius
    M_LUMINANCE,           // EvPrShrAmount
    M_LUMINANCE,           // EvPrShrThresh
    M_LUMINANCE,           // EvPrShrEdgeOnly
    M_LUMINANCE,           // EvPrShrEdgeRadius=375,
    M_LUMINANCE,           // EvPrShrEdgeTolerance=376,
    M_LUMINANCE,           // EvPrShrHaloControl=377,
    M_LUMINANCE,           // EvPrShrHaloAmount=378,
    M_LUMINANCE,           // EvPrShrMethod=379,
    M_LUMINANCE,           // EvPrShrDRadius=380,
    M_LUMINANCE,           // EvPrShrDAmount=381,
    M_LUMINANCE,           // EvPrShrDDamping=382,
    M_LUMINANCE,           // EvPrShrDIterations=383,
    DIRPYREQUALIZER,  // EvWavcbenab
    DIRPYREQUALIZER,  // EvWavgreenhigh
    DIRPYREQUALIZER,  // EvWavbluehigh
    DIRPYREQUALIZER,  // EvWavgreenmed
    DIRPYREQUALIZER,  // EvWavbluemed
    DIRPYREQUALIZER,  // EvWavgreenlow
    DIRPYREQUALIZER,  // EvWavbluelow
    DIRPYREQUALIZER,  // EvWavNeutral
    ALLNORAW, // EvDCPApplyLookTable,
    RGBCURVE|M_AUTOEXP, // EvDCPApplyBaselineExposureOffset,
    ALLNORAW,         // EvDCPApplyHueSatMap
    DIRPYREQUALIZER,  // EvWavenacont
    DIRPYREQUALIZER,  // EvWavenachrom
    DIRPYREQUALIZER,  // EvWavenaedge
    DIRPYREQUALIZER,  // EvWavenares
    DIRPYREQUALIZER,  // EvWavenafin
    DIRPYREQUALIZER,  // EvWavenatoning
    DIRPYREQUALIZER,  // EvWavenanoise
    DIRPYREQUALIZER,  // EvWavedgesensi
    DIRPYREQUALIZER,  // EvWavedgeampli
    DIRPYREQUALIZER,  // EvWavlev3nois
    DIRPYREQUALIZER,  // EvWavNPmet
    DEMOSAIC,         // EvretinexMethod
    0,          // EvLneigh
    0,          // EvLgain
    0,          // EvLoffs
    0,          // EvLstr
    0,          // EvLscal
    0,          // EvLvart
    DEMOSAIC,         // EvLCDCurve
    0,          // EvRetinextransmission
    DEMOSAIC,         // EvRetinexEnabled
    0,          // EvRetinexmedianmap
    0,          // EvLlimd
    DEMOSAIC,         // Evretinexcolorspace
    DEMOSAIC,         // EvLCDHCurve
    DEMOSAIC,         // Evretinexgamma
    DEMOSAIC,         // EvLgam
    DEMOSAIC,         // EvLslope
    0,          // EvLhighl
    0,                // --unused--
    DEMOSAIC,         // EvRetinexlhcurve
    OUTPUTPROFILE,    // EvOIntent
    MONITORTRANSFORM, // EvMonitorTransform: no history message
    0,          // EvLiter
    0,          // EvLgrad
    0,          // EvLgrads
    0,          // EvLhighlights
    0,          // EvLh_tonalwidth
    0,          // EvLshadows
    0,          // EvLs_tonalwidth
    0,          // EvLradius
    0,          // EvmapMethod
    DEMOSAIC,         // EvRetinexmapcurve
    DEMOSAIC,         // EvviewMethod
    ALLNORAW,         // EvcbdlMethod
    0,          // EvRetinexgaintransmission
    0,          // EvLskal
    OUTPUTPROFILE,    // EvOBPCompens
    WHITEBALANCE,      // EvWBtempBias
    DARKFRAME,        // EvRawImageNum
    0,                // unused
    0,                // unused
    0,                // unused
    DEMOSAIC,         // EvPixelShiftEperIso
    0,                // unused
    0,                // unused
    DEMOSAIC,         // EvPixelshiftShowMotion
    DEMOSAIC,         // EvPixelshiftShowMotionMaskOnly
    0,                // unused
    0,                // unused
    0,                // unused
    DEMOSAIC,         // EvPixelShiftNonGreenCross
    0,                // unused
    0,                // unused
    0,                // unused
    0,                // unused
    DEMOSAIC,         // EvPixelShiftGreen
    0,                // unused
    DEMOSAIC,         // EvPixelShiftBlur
    DEMOSAIC,         // EvPixelShiftSigma
    0,                // unused
    0,                // unused
    DEMOSAIC,         // EvPixelShiftHoleFill
    DEMOSAIC,         // EvPixelShiftMedian
    0,                // unused
    DEMOSAIC,         // EvPixelShiftMotionMethod
    DEMOSAIC,         // EvPixelShiftSmooth
    DEMOSAIC,         // EvPixelShiftLmmse
    DEMOSAIC,         // EvPixelShiftEqualBright
    DEMOSAIC,         // EvPixelShiftEqualBrightChannel
    LUMINANCECURVE,   // EvCATtempout
    LUMINANCECURVE,   // EvCATgreenout
    LUMINANCECURVE,   // EvCATybout
    LUMINANCECURVE,   // EvCATDegreeout
    LUMINANCECURVE,   // EvCATAutoDegreeout
    LUMINANCECURVE,   // EvCATtempsc
    LUMINANCECURVE,   // EvCATgreensc
    LUMINANCECURVE,   // EvCATybscen
    LUMINANCECURVE,   // EvCATAutoyb
    DARKFRAME,        // EvLensCorrMode
    DARKFRAME,        // EvLensCorrLensfunCamera
    DARKFRAME,        // EvLensCorrLensfunLens
    ALLNORAW,         // EvTMFattalEnabled
    HDR,              // EvTMFattalThreshold
    HDR,              // EvTMFattalAmount
    WHITEBALANCE,     // EvWBEnabled
    LUMINANCECURVE,   // EvRGBEnabled
    LUMINANCECURVE    // EvLEnabled
};


RefreshMapper::RefreshMapper():
    next_event_(rtengine::NUMOFEVENTS)
{
    for (int event = 0; event < rtengine::NUMOFEVENTS; ++event) {
        actions_[event] = refreshmap[event];
    }
}


ProcEvent RefreshMapper::newEvent()
{
    return ProcEvent(++next_event_);
}


void RefreshMapper::mapEvent(ProcEvent &event, int action)
{
    event.set_action(action);
}


int RefreshMapper::getAction(const ProcEvent &event) const
{
    auto it = actions_.find(event);
    if (it == actions_.end()) {
        return event.get_action();
    } else {
        return it->second;
    }
}


void RefreshMapper::setAction(ProcEvent &event, int action)
{
    mapEvent(event, action);
}


RefreshMapper *RefreshMapper::getInstance()
{
    static RefreshMapper instance;
    return &instance;
}

} // namespace rtengine
