/*
 * Copyright 2014 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkMatrixImageFilter.h"
#include "SkBitmap.h"
#include "SkCanvas.h"
#include "SkDevice.h"
#include "SkColorPriv.h"
#include "SkReadBuffer.h"
#include "SkWriteBuffer.h"
#include "SkMatrix.h"
#include "SkRect.h"

SkMatrixImageFilter::SkMatrixImageFilter(const SkMatrix& transform,
                                         SkFilterQuality filterQuality,
                                         SkImageFilter* input)
  : INHERITED(1, &input),
    fTransform(transform),
    fFilterQuality(filterQuality) {
}

SkMatrixImageFilter* SkMatrixImageFilter::Create(const SkMatrix& transform,
                                                 SkFilterQuality filterQuality,
                                                 SkImageFilter* input) {
    return new SkMatrixImageFilter(transform, filterQuality, input);
}

SkFlattenable* SkMatrixImageFilter::CreateProc(SkReadBuffer& buffer) {
    SK_IMAGEFILTER_UNFLATTEN_COMMON(common, 1);
    SkMatrix matrix;
    buffer.readMatrix(&matrix);
    SkFilterQuality quality = static_cast<SkFilterQuality>(buffer.readInt());
    return Create(matrix, quality, common.getInput(0).get());
}

void SkMatrixImageFilter::flatten(SkWriteBuffer& buffer) const {
    this->INHERITED::flatten(buffer);
    buffer.writeMatrix(fTransform);
    buffer.writeInt(fFilterQuality);
}

SkMatrixImageFilter::~SkMatrixImageFilter() {
}

bool SkMatrixImageFilter::onFilterImageDeprecated(Proxy* proxy,
                                                  const SkBitmap& source,
                                                  const Context& ctx,
                                                  SkBitmap* result,
                                                  SkIPoint* offset) const {
    SkBitmap src = source;
    SkIPoint srcOffset = SkIPoint::Make(0, 0);
    if (!this->filterInputDeprecated(0, proxy, source, ctx, &src, &srcOffset)) {
        return false;
    }

    SkRect dstRect;
    SkIRect srcBounds, dstBounds;
    src.getBounds(&srcBounds);
    srcBounds.offset(srcOffset);
    SkRect srcRect = SkRect::Make(srcBounds);
    SkMatrix matrix;
    if (!ctx.ctm().invert(&matrix)) {
        return false;
    }
    matrix.postConcat(fTransform);
    matrix.postConcat(ctx.ctm());
    matrix.mapRect(&dstRect, srcRect);
    dstRect.roundOut(&dstBounds);

    SkAutoTUnref<SkBaseDevice> device(proxy->createDevice(dstBounds.width(), dstBounds.height()));
    if (nullptr == device.get()) {
        return false;
    }

    SkCanvas canvas(device.get());
    canvas.translate(-SkIntToScalar(dstBounds.x()), -SkIntToScalar(dstBounds.y()));
    canvas.concat(matrix);
    SkPaint paint;

    paint.setXfermodeMode(SkXfermode::kSrc_Mode);
    paint.setFilterQuality(fFilterQuality);
    canvas.drawBitmap(src, srcRect.x(), srcRect.y(), &paint);

    *result = device.get()->accessBitmap(false);
    offset->fX = dstBounds.fLeft;
    offset->fY = dstBounds.fTop;
    return true;
}

SkRect SkMatrixImageFilter::computeFastBounds(const SkRect& src) const {
    SkRect bounds = this->getInput(0) ? this->getInput(0)->computeFastBounds(src) : src;
    SkRect dst;
    fTransform.mapRect(&dst, bounds);
    return dst;
}

SkIRect SkMatrixImageFilter::onFilterNodeBounds(const SkIRect& src, const SkMatrix& ctm,
                                                MapDirection direction) const {
    SkMatrix matrix;
    if (!ctm.invert(&matrix)) {
        return src;
    }
    if (kForward_MapDirection == direction) {
        matrix.postConcat(fTransform);
    } else {
        SkMatrix transformInverse;
        if (!fTransform.invert(&transformInverse)) {
            return src;
        }
        matrix.postConcat(transformInverse);
    }
    matrix.postConcat(ctm);
    SkRect floatBounds;
    matrix.mapRect(&floatBounds, SkRect::Make(src));
    return floatBounds.roundOut();
}

#ifndef SK_IGNORE_TO_STRING
void SkMatrixImageFilter::toString(SkString* str) const {
    str->appendf("SkMatrixImageFilter: (");

    str->appendf("transform: (%f %f %f %f %f %f %f %f %f)",
                 fTransform[SkMatrix::kMScaleX],
                 fTransform[SkMatrix::kMSkewX],
                 fTransform[SkMatrix::kMTransX],
                 fTransform[SkMatrix::kMSkewY],
                 fTransform[SkMatrix::kMScaleY],
                 fTransform[SkMatrix::kMTransY],
                 fTransform[SkMatrix::kMPersp0],
                 fTransform[SkMatrix::kMPersp1],
                 fTransform[SkMatrix::kMPersp2]);

    str->append("<dt>FilterLevel:</dt><dd>");
    static const char* gFilterLevelStrings[] = { "None", "Low", "Medium", "High" };
    str->append(gFilterLevelStrings[fFilterQuality]);
    str->append("</dd>");

    str->appendf(")");
}
#endif
