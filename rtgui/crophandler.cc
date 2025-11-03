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
#include "crophandler.h"
#undef THREAD_PRIORITY_NORMAL

#include <cstring>
#include "guiutils.h"
#include "cropwindow.h"
#include "imagearea.h"
#include "../rtengine/dcrop.h"
#include "../rtengine/refreshmap.h"
#include "../rtengine/rt_math.h"
#include "../rtengine/improcfun.h"
#include "../rtengine/gauss.h"

#include "../rtengine/threadpool.h"

#ifdef _OPENMP
# include <omp.h>
#endif

using namespace rtengine;

namespace {


Glib::RefPtr<Gdk::Pixbuf> resize_fast(Glib::RefPtr<Gdk::Pixbuf> src, int dw, int dh, float scale)
{
    auto dst = Gdk::Pixbuf::create(Gdk::COLORSPACE_RGB, false, 8, dw, dh);
    src->scale(dst, 0, 0, dw, dh, 0, 0, scale, scale, Gdk::INTERP_NEAREST);
    return dst;
}


Glib::RefPtr<Gdk::Pixbuf> resize_lanczos(Glib::RefPtr<Gdk::Pixbuf> src, int dw, int dh, float scale)
{
    const int sW = src->get_width();
    const int sH = src->get_height();

    Imagefloat tmps(sW, sH);
    tmps.assignMode(Imagefloat::Mode::LAB);
    auto s_data = src->get_pixels();
    auto s_stride = src->get_rowstride();
        
#ifdef _OPENMP
#   pragma omp parallel for
#endif
    for (int y = 0; y < sH; ++y) {
        auto s_row = s_data + (y * s_stride);
        for (int x = 0; x < sW; ++x) {
            tmps.r(y, x) = s_row[x * 3];
            tmps.g(y, x) = s_row[x * 3 + 1];
            tmps.b(y, x) = s_row[x * 3 + 2];
        }
    }

    Imagefloat tmpd(dw, dh);

    ImProcFunctions ipf(nullptr);
    ipf.Lanczos(&tmps, &tmpd, scale);

    auto dst = Gdk::Pixbuf::create(Gdk::COLORSPACE_RGB, false, 8, dw, dh);
    auto d_data = dst->get_pixels();
    auto d_stride = dst->get_rowstride();
    
#ifdef _OPENMP
#   pragma omp parallel for
#endif
    for (int y = 0; y < dh; ++y) {
        auto d_row = d_data + (y * d_stride);
        for (int x = 0; x < dw; ++x) {
            d_row[x * 3] = LIM(int(tmpd.r(y, x)), 0, 255); 
            d_row[x * 3 + 1] = LIM(int(tmpd.g(y, x)), 0, 255);
            d_row[x * 3 + 2] = LIM(int(tmpd.b(y, x)), 0, 255);
        }
    }

    return dst;
}


void sharpen_for_display(Glib::RefPtr<Gdk::Pixbuf> src, int skip)
{
    if (skip <= 1) {
        return;
    }

    const int sW = src->get_width();
    const int sH = src->get_height();

    array2D<float> buf(sW, sH);

    auto s_data = src->get_pixels();
    auto s_stride = src->get_rowstride();

    const double sigma = 0.5 * 2.0/double(skip);
    const float amount = 0.8;

#ifdef _OPENMP
#   pragma omp parallel
#endif
    {
#ifdef _OPENMP
#       pragma omp for
#endif
        for (int y = 0; y < sH; ++y) {
            auto s_row = s_data + (y * s_stride);
            for (int x = 0; x < sW; ++x) {
                buf[y][x] = s_row[x * 3 + 1];
            }
        }

        gaussianBlur(buf, buf, sW, sH, sigma);

#ifdef _OPENMP
#       pragma omp for
#endif
        for (int y = 0; y < sH; ++y) {
            auto s_row = s_data + (y * s_stride);
            for (int x = 0; x < sW; ++x) {
                float diff = s_row[x * 3 + 1] - buf[y][x];
                s_row[x * 3 + 1] = LIM(int(s_row[x * 3 + 1] + diff * amount), 0, 255);
            }
        }
    }
}


Glib::RefPtr<Gdk::Pixbuf> resample_preview(Glib::RefPtr<Gdk::Pixbuf> src, int dw, int dh, float scale, int skip)
{
    if (scale > 1.f || options.preview_resampling_quality == Options::PreviewResamplingQuality::LOW) {
        src = resize_fast(src, dw, dh, scale);
    } else if (scale < 1.f) {
        src = resize_lanczos(src, dw, dh, scale);
    }
    if (options.preview_resampling_quality == Options::PreviewResamplingQuality::HIGH) {
        sharpen_for_display(src, skip);
    }
    return src;
}


} // namespace


CropHandler::CropHandler() :
    zoom(100),
    ww(0),
    wh(0),
    cax(-1),
    cay(-1),
    cx(0),
    cy(0),
    cw(0),
    ch(0),
    cropX(0),
    cropY(0),
    cropW(0),
    cropH(0),
    enabled(false),
    cropimg_width(0),
    cropimg_height(0),
    cix(0),
    ciy(0),
    ciw(0),
    cih(0),
    cis(1),
    ipc(nullptr),
    crop(nullptr),
    displayHandler(nullptr),
    redraw_needed(false),
    initial(false)
{
}


CropHandler::~CropHandler()
{
    idle_register.destroy();

    if (ipc) {
        ipc->delSizeListener (this);
    }

    setEnabled (false);

    if (crop) {
        //crop->destroy ();
        delete crop; // will do the same than destroy, plus delete the object
        crop = nullptr;
    }
}

void CropHandler::setEditSubscriber (EditSubscriber* newSubscriber)
{
    (static_cast<rtengine::Crop *>(crop))->setEditSubscriber(newSubscriber);
}


void CropHandler::newImage(std::shared_ptr<StagedImageProcessor> ipc_, bool isDetailWindow)
{
    ipc = ipc_;
    cx = 0;
    cy = 0;

    if (!ipc) {
        return;
    }

    EditDataProvider *editDataProvider = nullptr;
    CropWindow *cropWin = displayHandler ? static_cast<CropWindow*>(displayHandler) : nullptr;

    if (cropWin) {
        editDataProvider = cropWin->getImageArea();
    }

    crop = ipc->createCrop (editDataProvider, isDetailWindow);
    ipc->setSizeListener (this);
    crop->setListener (enabled ? this : nullptr);
    initial = true;
}

void CropHandler::sizeChanged(int x, int y, int ow, int oh)    // the ipc notifies it to keep track size changes like rotation
{
    compDim ();
}

bool CropHandler::isFullDisplay ()
{
    int w, h;
    getFullImageSize(w, h);
    if (!w) {
        return false;
    }
    return cropW == w && cropH == h;
}

double CropHandler::getFitCropZoom ()
{
    double z1 = (double) wh / cropParams.h;
    double z2 = (double) ww / cropParams.w;
    return z1 < z2 ? z1 : z2;
}

double CropHandler::getFitZoom ()
{
    if (ipc) {
        double z1 = (double) wh / ipc->getFullHeight ();
        double z2 = (double) ww / ipc->getFullWidth ();
        return z1 < z2 ? z1 : z2;
    } else {
        return 1.0;
    }
}

void CropHandler::setZoom (int z, int centerx, int centery)
{
    //assert (ipc);
    if (!ipc) {
        return;
    }

    int oldZoom = zoom;
    // TODO(zoulu): Find better way to avoid hard coding 1000 as the threshold
    float oldScale = zoom >= 1000 ? float(zoom) / 1000.0f : 10.f / float(zoom);
    float newScale = z >= 1000 ? float(z) / 1000.0f : 10.f / float(z);

    int oldcax = cax;
    int oldcay = cay;

    if (centerx == -1) {
        cax = ipc->getFullWidth () / 2;
    } else {
        float distToAnchor = float(cax - centerx);
        distToAnchor = distToAnchor / newScale * oldScale;
        cax = centerx + int(distToAnchor);
    }

    if (centery == -1) {
        cay = ipc->getFullHeight () / 2;
    } else {
        float distToAnchor = float(cay - centery);
        distToAnchor = distToAnchor / newScale * oldScale;
        cay = centery + int(distToAnchor);
    }

    // maybe demosaic etc. if we cross the border to >100%
    bool needsFullRefresh = (z >= 1000 && zoom < 1000);

    zoom = z;

    if (zoom >= 1000) {
        cw = ww * 1000 / zoom;
        ch = wh * 1000 / zoom;
    } else {
        cw = ww * (zoom / 10);
        ch = wh * (zoom / 10);
    }

    cx = cax - cw / 2;
    cy = cay - ch / 2;


    int oldCropX = cropX;
    int oldCropY = cropY;
    int oldCropW = cropW;
    int oldCropH = cropH;

    compDim ();

    bool needed = enabled && (oldZoom != zoom || oldcax != cax || oldcay != cay || oldCropX != cropX || oldCropY != cropY || oldCropW != cropW || oldCropH != cropH);

    if (needed) {
        const auto doit =
            [this,needsFullRefresh]() -> bool
            {
                if (ipc) {
                    if (needsFullRefresh && !ipc->getHighQualComputed()) {
                        ipc->startProcessing(M_HIGHQUAL);
                        ipc->setHighQualComputed();
                    } else {
                        update ();
                    }
                }
                return false;
            };

        if (zoom_conn_.connected()) {
            zoom_conn_.disconnect();
        }
        if (cropPixbuf) {
            cropPixbuf.clear();
        }
        if (cropPixbuftrue) {
            cropPixbuftrue.clear();
        }

        zoom_conn_ = Glib::signal_timeout().connect(sigc::slot<bool>(doit), options.adjusterMaxDelay);
    }
    
    // if (enabled && (oldZoom != zoom || oldcax != cax || oldcay != cay || oldCropX != cropX || oldCropY != cropY || oldCropW != cropW || oldCropH != cropH)) {
    //     if (needsFullRefresh && !ipc->getHighQualComputed()) {
    //         cropPixbuf.clear ();
    //         ipc->startProcessing(M_HIGHQUAL);
    //         ipc->setHighQualComputed();
    //     } else {
    //         update ();
    //     }
    // }

}

float CropHandler::getZoomFactor ()
{
    if (zoom >= 1000) {
        return zoom / 1000;
    } else {
        return 10.f / (float)zoom;
    }
}


void CropHandler::setWSize (int w, int h)
{

    ww = w;
    wh = h;

    if (zoom >= 1000) {
        cw = ww * 1000 / zoom;
        ch = wh * 1000 / zoom;
    } else {
        cw = ww * (zoom / 10);
        ch = wh * (zoom / 10);
    }

    compDim ();

    if (enabled) {
        update ();
    }
}

void CropHandler::getWSize (int& w, int &h)
{

    w = ww;
    h = wh;
}

void CropHandler::getAnchorPosition (int& x, int& y)
{
    x = cax;
    y = cay;
}

void CropHandler::setAnchorPosition (int x, int y, bool update_)
{
    cax = x;
    cay = y;

    compDim ();

    if (enabled && update_) {
        update ();
    }
}

void CropHandler::moveAnchor (int deltaX, int deltaY, bool update_)
{
    cax += deltaX;
    cay += deltaY;

    compDim ();

    if (enabled && update_) {
        update ();
    }
}

void CropHandler::centerAnchor (bool update_)
{
    assert (ipc);

    // Computes the crop's size and position given the anchor's position and display size

    cax = ipc->getFullWidth() / 2;
    cay = ipc->getFullHeight() / 2;

    compDim ();

    if (enabled && update_) {
        update ();
    }
}

void CropHandler::getPosition (int& x, int& y)
{

    x = cropX;
    y = cropY;
}


void CropHandler::setDetailedCrop(
    IImage8* im,
    IImage8* imtrue,
    const rtengine::procparams::ColorManagementParams& cmp,
    const rtengine::procparams::CropParams& cp,
    int ax,
    int ay,
    int aw,
    int ah,
    int askip
)
{
    if (!enabled) {
        return;
    }

    cimg.lock ();

    cropParams = cp;
    colorParams = cmp;

    if (!cropimg.empty()) {
        cropimg.clear();
    }

    if (!cropimgtrue.empty()) {
        cropimgtrue.clear();
    }

    if (ax == cropX && ay == cropY && aw == cropW && ah == cropH && askip == (zoom >= 1000 ? 1 : zoom / 10)) {
        cropimg_width = im->getWidth ();
        cropimg_height = im->getHeight ();
        const std::size_t cropimg_size = 3 * cropimg_width * cropimg_height;
        cropimg.assign(im->getData(), im->getData() + cropimg_size);
        cropimgtrue.assign(imtrue->getData(), imtrue->getData() + cropimg_size);
        cix = ax;
        ciy = ay;
        ciw = aw;
        cih = ah;
        cis = askip;

        bool expected = false;

        if (redraw_needed.compare_exchange_strong(expected, true)) {
            idle_register.add(
                [this]() -> bool
                {
                    cimg.lock ();

                    if (redraw_needed.exchange(false)) {
                        cropPixbuf.clear ();
                        cropPixbuftrue.clear ();

                        if (!enabled) {
                            cropimg.clear();
                            cropimgtrue.clear();
                            cimg.unlock ();
                            return false;
                        }

                        if (!cropimg.empty()) {
                            if (cix == cropX && ciy == cropY && ciw == cropW && cih == cropH && cis == (zoom >= 1000 ? 1 : zoom / 10)) {
                                // calculate final image size
                                float czoom = zoom >= 1000 ?
                                    zoom / 1000.f :
                                    float((zoom/10) * 10) / float(zoom);
                                int imw = cropimg_width * czoom;
                                int imh = cropimg_height * czoom;

                                if (imw > ww) {
                                    imw = ww;
                                }

                                if (imh > wh) {
                                    imh = wh;
                                }

                                cropPixbuf = Gdk::Pixbuf::create_from_data (cropimg.data(), Gdk::COLORSPACE_RGB, false, 8, cropimg_width, cropimg_height, 3 * cropimg_width);
                                cropPixbuf = resample_preview(cropPixbuf, imw, imh, czoom, cis);
                                // if (czoom < 1.f) {
                                //     cropPixbuf = resize_lanczos(cropPixbuf, imw, imh, czoom);
                                // } else if (czoom > 1.f) {
                                //     cropPixbuf = resize_fast(cropPixbuf, imw, imh, czoom);
                                // }

                                // sharpen_for_display(cropPixbuf, cis);

                                cropPixbuftrue = Gdk::Pixbuf::create_from_data (cropimgtrue.data(), Gdk::COLORSPACE_RGB, false, 8, cropimg_width, cropimg_height, 3 * cropimg_width);
                                if (czoom != 1.f) {
                                    cropPixbuftrue = resize_fast(cropPixbuftrue, imw, imh, czoom);
                                }
                            }

                            cropimg.clear();
                            cropimgtrue.clear();
                        }

                        cimg.unlock ();

                        if (displayHandler) {
                            displayHandler->cropImageUpdated ();

                            if (initial.exchange(false)) {
                                displayHandler->initialImageArrived ();
                            }
                        }
                    } else {
                        cimg.unlock();
                    }

                    return false;
                }
            );
        }
    }

    cimg.unlock ();
}

void CropHandler::getWindow(int& cwx, int& cwy, int& cww, int& cwh, int& cskip)
{
    cwx = cropX;
    cwy = cropY;
    cww = cropW;
    cwh = cropH;

    // hack: if called before first size allocation the size will be 0
    if (cww == 0) {
        cww = 10;
    }

    if (cwh == 0) {
        cwh = 32;
    }

    cskip = zoom >= 1000 ? 1 : zoom/10;
}

void CropHandler::update ()
{

    if (crop && enabled) {
//        crop->setWindow (cropX, cropY, cropW, cropH, zoom>=1000 ? 1 : zoom); --> we use the "getWindow" hook instead of setting the size before
        crop->setListener (this);
        cimg.lock();
        cropPixbuf.clear ();
        cropPixbuftrue.clear();
        cimg.unlock();

        // To save threads, try to mark "needUpdate" without a thread first
        if (crop->tryUpdate()) {
            auto i = ipc; // keep a reference around so that ipc doesn't get
                          // destroyed
            auto c = crop;
            const auto upd =
                [i, c]() -> void
                {
                    c->fullUpdate();
                };
            rtengine::ThreadPool::add_task(rtengine::ThreadPool::Priority::HIGH, upd);
        }
    }
}

void CropHandler::setEnabled(bool e, bool do_update)
{

    enabled = e;

    if (!enabled) {
        if (crop) {
            crop->setListener (nullptr);
        }

        cimg.lock();
        cropimg.clear();
        cropimgtrue.clear();
        cropPixbuf.clear();
        cimg.unlock();
    } else if (do_update) {
        update();
    }
}

bool CropHandler::getEnabled ()
{

    return enabled;
}

void CropHandler::colorPick (const rtengine::Coord &pickerPos, float &r, float &g, float &b, float &rpreview, float &gpreview, float &bpreview, LockableColorPicker::Size size)
{

    //MyMutex::MyLock lock(cimg);

    if (!cropPixbuf || !cropPixbuftrue) {
        r = g = b = 0.f;
        rpreview = gpreview = bpreview = 0.f;
        return;
    }

    int xSize = (int)size;
    int ySize = (int)size;
    int pixbufW = cropPixbuftrue->get_width();
    int pixbufH = cropPixbuftrue->get_height();
    rtengine::Coord topLeftPos(pickerPos.x - xSize/2, pickerPos.y - ySize/2);

    if (topLeftPos.x > pixbufW || topLeftPos.y > pixbufH || topLeftPos.x + xSize < 0 || topLeftPos.y + ySize < 0) {
        return;
    }

    // Store the position of the center of the picker
    int radius = (int)size / 2;

    // X/Width clip
    if (topLeftPos.x < 0) {
        xSize += topLeftPos.x;
        topLeftPos.x = 0;
    }
    if (topLeftPos.x + xSize > pixbufW) {
        xSize = pixbufW - topLeftPos.x;
    }
    // Y/Height clip
    if (topLeftPos.y < 0) {
        ySize += topLeftPos.y;
        topLeftPos.y = 0;
    }
    if (topLeftPos.y + ySize > pixbufH) {
        ySize = pixbufH - topLeftPos.y;
    }

    // Accumulating the data
    std::uint32_t r2=0, g2=0, b2=0;
    std::uint32_t count = 0;
    const guint8* data = cropPixbuftrue->get_pixels();
    for (int j = topLeftPos.y ; j < topLeftPos.y + ySize ; ++j) {
        const guint8* data2 = data + cropPixbuftrue->get_rowstride()*j;
        for (int i = topLeftPos.x ; i < topLeftPos.x + xSize ; ++i) {
            const guint8* data3 = data2 + i*3;
            rtengine::Coord currPos(i, j);
            rtengine::Coord delta = pickerPos - currPos;
            rtengine::PolarCoord p(delta);
            if (p.radius <= radius) {
                r2 += *data3;
                g2 += *(data3+1);
                b2 += *(data3+2);
                ++count;
            }
        }
    }

    count = std::max(1u, count);
    // Averaging
    r = (float)r2 / (float)count / 255.f;
    g = (float)g2 / (float)count / 255.f;
    b = (float)b2 / (float)count / 255.f;

    // Accumulating the data
    r2=0, g2=0, b2=0;
    count = 0;
    data = cropPixbuf->get_pixels();
    for (int j = topLeftPos.y ; j < topLeftPos.y + ySize ; ++j) {
        const guint8* data2 = data + cropPixbuf->get_rowstride()*j;
        for (int i = topLeftPos.x ; i < topLeftPos.x + xSize ; ++i) {
            const guint8* data3 = data2 + i*3;
            rtengine::Coord currPos(i, j);
            rtengine::Coord delta = pickerPos - currPos;
            rtengine::PolarCoord p(delta);
            if (p.radius <= radius) {
                r2 += *data3;
                g2 += *(data3+1);
                b2 += *(data3+2);
                ++count;
            }
        }
    }

    count = std::max(1u, count);
    // Averaging
    rpreview = (float)r2 / (float)count / 255.f;
    gpreview = (float)g2 / (float)count / 255.f;
    bpreview = (float)b2 / (float)count / 255.f;
}

void CropHandler::getSize (int& w, int& h)
{

    w = cropW;
    h = cropH;
}

void CropHandler::getFullImageSize (int& w, int& h)
{
    if (ipc) {
        w = ipc->getFullWidth ();
        h = ipc->getFullHeight ();
    } else {
        w = h = 0;
    }
}

void CropHandler::compDim ()
{
    assert (ipc && displayHandler);

    // Computes the crop's size and position given the anchor's position and display size

    int fullW = ipc->getFullWidth();
    int fullH = ipc->getFullHeight();
    int imgX = -1, imgY = -1;
    //int scaledFullW, scaledFullH;
    int scaledCAX, scaledCAY;
    int wwImgSpace;
    int whImgSpace;

    cax = rtengine::LIM(cax, 0, fullW-1);
    cay = rtengine::LIM(cay, 0, fullH-1);

    if (zoom >= 1000) {
        wwImgSpace = int(float(ww) / float(zoom/1000) + 0.5f);
        whImgSpace = int(float(wh) / float(zoom/1000) + 0.5f);
        //scaledFullW = fullW * (zoom/1000);
        //scaledFullH = fullH * (zoom/1000);
        scaledCAX = cax * (zoom/1000);
        scaledCAY = cay * (zoom/1000);
    } else {
        wwImgSpace = int(float(ww) * (float(zoom)/10.f) + 0.5f);
        whImgSpace = int(float(wh) * (float(zoom)/10.f) + 0.5f);
        //scaledFullW = fullW / zoom;
        //scaledFullH = fullH / zoom;
        scaledCAX = int(float(cax) / (float(zoom)/10.f));
        scaledCAY = int(float(cay) / (float(zoom)/10.f));
    }

    imgX = ww / 2 - scaledCAX;
    if (imgX < 0) {
        imgX = 0;
    }
    imgY = wh / 2 - scaledCAY;
    if (imgY < 0) {
        imgY = 0;
    }

    cropX = cax - (wwImgSpace/2);
    cropY = cay - (whImgSpace/2);
    cropW = wwImgSpace;
    cropH = whImgSpace;

    if (cropX + cropW > fullW) {
        cropW = fullW - cropX;
    }

    if (cropY + cropH > fullH) {
        cropH = fullH - cropY;
    }

    if (cropX < 0) {
        cropW += cropX;
        cropX = 0;
    }

    if (cropY < 0) {
        cropH += cropY;
        cropY = 0;
    }

    // Should be good already, but this will correct eventual rounding error

    if (cropW > fullW) {
        cropW = fullW;
    }

    if (cropH > fullH) {
        cropH = fullH;
    }

    displayHandler->setDisplayPosition(imgX, imgY);
}
