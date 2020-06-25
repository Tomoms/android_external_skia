/*
 * Copyright 2014 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef GrFragmentProcessor_DEFINED
#define GrFragmentProcessor_DEFINED

#include <tuple>

#include "include/private/SkSLSampleMatrix.h"
#include "src/gpu/GrCoordTransform.h"
#include "src/gpu/GrProcessor.h"
#include "src/gpu/ops/GrOp.h"

class GrGLSLFragmentProcessor;
class GrPaint;
class GrPipeline;
class GrProcessorKeyBuilder;
class GrShaderCaps;
class GrSwizzle;

/** Provides custom fragment shader code. Fragment processors receive an input color (half4) and
    produce an output color. They may reference textures and uniforms. They may use
    GrCoordTransforms to receive a transformation of the local coordinates that map from local space
    to the fragment being processed.
 */
class GrFragmentProcessor : public GrProcessor {
public:
    class TextureSampler;

    /**
    *  In many instances (e.g. SkShader::asFragmentProcessor() implementations) it is desirable to
    *  only consider the input color's alpha. However, there is a competing desire to have reusable
    *  GrFragmentProcessor subclasses that can be used in other scenarios where the entire input
    *  color is considered. This function exists to filter the input color and pass it to a FP. It
    *  does so by returning a parent FP that multiplies the passed in FPs output by the parent's
    *  input alpha. The passed in FP will not receive an input color.
    */
    static std::unique_ptr<GrFragmentProcessor> MulChildByInputAlpha(
            std::unique_ptr<GrFragmentProcessor> child);

    /**
     *  Like MulChildByInputAlpha(), but reverses the sense of src and dst. In this case, return
     *  the input modulated by the child's alpha. The passed in FP will not receive an input color.
     *
     *  output = input * child.a
     */
    static std::unique_ptr<GrFragmentProcessor> MulInputByChildAlpha(
            std::unique_ptr<GrFragmentProcessor> child);

    /**
     *  This assumes that the input color to the returned processor will be unpremul and that the
     *  passed processor (which becomes the returned processor's child) produces a premul output.
     *  The result of the returned processor is a premul of its input color modulated by the child
     *  processor's premul output.
     */
    static std::unique_ptr<GrFragmentProcessor> MakeInputPremulAndMulByOutput(
            std::unique_ptr<GrFragmentProcessor>);

    /**
     *  Returns a parent fragment processor that adopts the passed fragment processor as a child.
     *  The parent will ignore its input color and instead feed the passed in color as input to the
     *  child.
     */
    static std::unique_ptr<GrFragmentProcessor> OverrideInput(std::unique_ptr<GrFragmentProcessor>,
                                                              const SkPMColor4f&,
                                                              bool useUniform = true);

    /**
     *  Returns a fragment processor that premuls the input before calling the passed in fragment
     *  processor.
     */
    static std::unique_ptr<GrFragmentProcessor> PremulInput(std::unique_ptr<GrFragmentProcessor>);

    /**
     *  Returns a fragment processor that calls the passed in fragment processor, and then swizzles
     *  the output.
     */
    static std::unique_ptr<GrFragmentProcessor> SwizzleOutput(std::unique_ptr<GrFragmentProcessor>,
                                                              const GrSwizzle&);

    /**
     *  Returns a fragment processor that calls the passed in fragment processor, and then ensures
     *  the output is a valid premul color by clamping RGB to [0, A].
     */
    static std::unique_ptr<GrFragmentProcessor> ClampPremulOutput(
            std::unique_ptr<GrFragmentProcessor>);

    /**
     * Returns a fragment processor that runs the passed in array of fragment processors in a
     * series. The original input is passed to the first, the first's output is passed to the
     * second, etc. The output of the returned processor is the output of the last processor of the
     * series.
     *
     * The array elements with be moved.
     */
    static std::unique_ptr<GrFragmentProcessor> RunInSeries(std::unique_ptr<GrFragmentProcessor>[],
                                                            int cnt);

    /**
     * Makes a copy of this fragment processor that draws equivalently to the original.
     * If the processor has child processors they are cloned as well.
     */
    virtual std::unique_ptr<GrFragmentProcessor> clone() const = 0;

    // The FP this was registered with as a child function. This will be null if this is a root.
    const GrFragmentProcessor* parent() const { return fParent; }

    GrGLSLFragmentProcessor* createGLSLInstance() const;

    void getGLSLProcessorKey(const GrShaderCaps& caps, GrProcessorKeyBuilder* b) const {
        this->onGetGLSLProcessorKey(caps, b);
        for (int i = 0; i < fChildProcessors.count(); ++i) {
            fChildProcessors[i]->getGLSLProcessorKey(caps, b);
        }
    }

    int numTextureSamplers() const { return fTextureSamplerCnt; }
    const TextureSampler& textureSampler(int i) const;

    int numCoordTransforms() const;
    const GrCoordTransform& coordTransform(int index) const;

    int numChildProcessors() const { return fChildProcessors.count(); }

    GrFragmentProcessor& childProcessor(int index) { return *fChildProcessors[index]; }
    const GrFragmentProcessor& childProcessor(int index) const { return *fChildProcessors[index]; }

    SkDEBUGCODE(bool isInstantiated() const;)

    /**
     * Do any of the FPs in this tree require local coordinates to be produced by the primitive
     * processor. This can return true even if this FP does not refer to sample coordinates, but
     * true if a descendant FP uses them.  FPs that are sampled explicitly do not
     * require primitive-generated local coordinates.
     *
     * If the root of an FP tree does not provide explicit coordinates, the geometry processor
     * provides the original local coordinates to start. This may be implicit as part of vertex
     * shader-lifted varyings, or by providing the base local coordinate to the fragment shader.
     */
    bool sampleCoordsDependOnLocalCoords() const {
        return (SkToBool(fFlags & kHasCoordTransforms_Flag) ||
                SkToBool(fFlags & kUsesSampleCoordsDirectly_Flag) ||
                SkToBool(fFlags & kUsesSampleCoordsIndirectly_Flag)) &&
               !SkToBool(fFlags & kSampledWithExplicitCoords_Flag);
    }

   /**
     * True if this FP refers directly to the sample coordinate parameter of its function
     * (e.g. uses EmitArgs::fSampleCoord in emitCode()). This also returns true if the
     * coordinate reference comes from autogenerated code invoking 'sample(matrix)' expressions.
     *
     * Unlike sampleCoordsDependOnLocalCoords(), this can return true whether or not the FP is
     * explicitly sampled, and does not change based on how the FP is composed. This property is
     * specific to the FP's function and not the entire program.
     */
    bool referencesSampleCoords() const {
        // HasCoordTransforms propagates up the FP tree, but we want the presence of an actual
        // coord transform object (that's not one of the implicit workarounds).
        return SkToBool(fFlags & kUsesSampleCoordsDirectly_Flag) || fCoordTransforms.count() > 0;
    }

    // True if this FP's parent invokes it with 'sample(float2)' or a variable 'sample(matrix)'
    bool isSampledWithExplicitCoords() const {
        return SkToBool(fFlags & kSampledWithExplicitCoords_Flag);
    }

    // True if the transform chain from root to this FP introduces perspective into the local
    // coordinate expression.
    bool hasPerspectiveTransform() const {
        return SkToBool(fFlags & kNetTransformHasPerspective_Flag);
    }

    // The SampleMatrix describing how this FP is invoked by its parent using 'sample(matrix)'
    // This only reflects the immediate sampling from parent to this FP
    const SkSL::SampleMatrix& sampleMatrix() const {
        return fMatrix;
    }

    /**
     * A GrDrawOp may premultiply its antialiasing coverage into its GrGeometryProcessor's color
     * output under the following scenario:
     *   * all the color fragment processors report true to this query,
     *   * all the coverage fragment processors report true to this query,
     *   * the blend mode arithmetic allows for it it.
     * To be compatible a fragment processor's output must be a modulation of its input color or
     * alpha with a computed premultiplied color or alpha that is in 0..1 range. The computed color
     * or alpha that is modulated against the input cannot depend on the input's alpha. The computed
     * value cannot depend on the input's color channels unless it unpremultiplies the input color
     * channels by the input alpha.
     */
    bool compatibleWithCoverageAsAlpha() const {
        return SkToBool(fFlags & kCompatibleWithCoverageAsAlpha_OptimizationFlag);
    }

    /**
     * If this is true then all opaque input colors to the processor produce opaque output colors.
     */
    bool preservesOpaqueInput() const {
        return SkToBool(fFlags & kPreservesOpaqueInput_OptimizationFlag);
    }

    /**
     * Tests whether given a constant input color the processor produces a constant output color
     * (for all fragments). If true outputColor will contain the constant color produces for
     * inputColor.
     */
    bool hasConstantOutputForConstantInput(SkPMColor4f inputColor, SkPMColor4f* outputColor) const {
        if (fFlags & kConstantOutputForConstantInput_OptimizationFlag) {
            *outputColor = this->constantOutputForConstantInput(inputColor);
            return true;
        }
        return false;
    }
    bool hasConstantOutputForConstantInput() const {
        return SkToBool(fFlags & kConstantOutputForConstantInput_OptimizationFlag);
    }

    /** Returns true if this and other processor conservatively draw identically. It can only return
        true when the two processor are of the same subclass (i.e. they return the same object from
        from getFactory()).

        A return value of true from isEqual() should not be used to test whether the processor would
        generate the same shader code. To test for identical code generation use getGLSLProcessorKey
     */
    bool isEqual(const GrFragmentProcessor& that) const;

    void visitProxies(const GrOp::VisitProxyFunc& func);

    // A pre-order traversal iterator over a hierarchy of FPs. It can also iterate over all the FP
    // hierarchies rooted in a GrPaint, GrProcessorSet, or GrPipeline. For these collections it
    // iterates the tree rooted at each color FP and then each coverage FP.
    //
    // Iter is the non-const version and CIter is the const version.
    //
    // An iterator is constructed from one of the srcs and used like this:
    //   for (GrFragmentProcessor::Iter iter(pipeline); iter; ++iter) {
    //       GrFragmentProcessor& fp = *iter;
    //   }
    // The exit test for the loop is using Iter's operator bool().
    // To use a range-for loop instead see CIterRange below.
    class Iter;
    class CIter;

    // Used to implement a range-for loop using CIter. Src is one of GrFragmentProcessor,
    // GrPaint, GrProcessorSet, or GrPipeline. Type aliases for these defined below.
    // Example usage:
    //   for (const auto& fp : GrFragmentProcessor::PaintRange(paint)) {
    //       if (fp.usesLocalCoords()) {
    //       ...
    //       }
    //   }
    template <typename Src> class CIterRange;
    // Like CIterRange but non const and only constructable from GrFragmentProcessor. This could
    // support GrPaint as it owns non-const FPs but no need for it as of now.
    //   for (auto& fp0 : GrFragmentProcessor::IterRange(fp)) {
    //       ...
    //   }
    class IterRange;

    // We would use template deduction guides for Iter/CIter but for:
    // https://gcc.gnu.org/bugzilla/show_bug.cgi?id=79501
    // Instead we use these specialized type aliases to make it prettier
    // to construct Iters for particular sources of FPs.
    using FPCRange = CIterRange<GrFragmentProcessor>;
    using PaintCRange = CIterRange<GrPaint>;

    // Implementation details for iterators that walk an array of Items owned by a set of FPs.
    using CountFn = int (GrFragmentProcessor::*)() const;
    // Defined GetFn to be a member function that returns an Item by index. The function itself is
    // const if Item is a const type and non-const if Item is non-const.
    template <typename Item, bool IsConst = std::is_const<Item>::value> struct GetT;
    template <typename Item> struct GetT<Item, false> {
        using GetFn = Item& (GrFragmentProcessor::*)(int);
    };
    template <typename Item> struct GetT<Item, true> {
        using GetFn = const Item& (GrFragmentProcessor::*)(int) const;
    };
    template <typename Item> using GetFn = typename GetT<Item>::GetFn;
    // This is an iterator over the Items owned by a (collection of) FP. CountFn is a FP member that
    // gets the number of Items owned by each FP and GetFn is a member that gets them by index.
    template <typename Item, CountFn Count, GetFn<Item> Get> class FPItemIter;

    // Loops over all the GrCoordTransforms owned by GrFragmentProcessors. The possible sources for
    // the iteration are the same as those for Iter and the FPs are walked in the same order as
    // Iter. This provides access to the coord transform and the FP that owns it. Example usage:
    //   for (GrFragmentProcessor::CoordTransformIter iter(pipeline); iter; ++iter) {
    //       // transform is const GrCoordTransform& and owningFP is const GrFragmentProcessor&.
    //       auto [transform, owningFP] = *iter;
    //       ...
    //   }
    // See the ranges below to make this simpler a la range-for loops.
    using CoordTransformIter = FPItemIter<const GrCoordTransform,
                                          &GrFragmentProcessor::numCoordTransforms,
                                          &GrFragmentProcessor::coordTransform>;
    // Same as CoordTransformIter but for TextureSamplers:
    //   for (GrFragmentProcessor::TextureSamplerIter iter(pipeline); iter; ++iter) {
    //       // TextureSamplerIter is const GrFragmentProcessor::TextureSampler& and
    //       // owningFP is const GrFragmentProcessor&.
    //       auto [sampler, owningFP] = *iter;
    //       ...
    //   }
    // See the ranges below to make this simpler a la range-for loops.
    using TextureSamplerIter = FPItemIter<const TextureSampler,
                                          &GrFragmentProcessor::numTextureSamplers,
                                          &GrFragmentProcessor::textureSampler>;

    // Implementation detail for using CoordTransformIter and TextureSamplerIter in range-for loops.
    template <typename Src, typename ItemIter> class FPItemRange;

    // These allow iteration over coord transforms/texture samplers for various FP sources via
    // range-for loops. An example usage for looping over the coord transforms in a pipeline:
    // for (auto [transform, fp] : GrFragmentProcessor::PipelineCoordTransformRange(pipeline)) {
    //     ...
    // }
    // Only the combinations of FP sources and iterable things have been defined but it is easy
    // to add more as they become useful. Maybe someday we'll have template argument deduction
    // with guides for type aliases and the sources can be removed from the type aliases:
    // http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p1021r5.html
    using PipelineCoordTransformRange = FPItemRange<const GrPipeline, CoordTransformIter>;
    using PipelineTextureSamplerRange = FPItemRange<const GrPipeline, TextureSamplerIter>;
    using FPTextureSamplerRange = FPItemRange<const GrFragmentProcessor, TextureSamplerIter>;
    using ProcessorSetTextureSamplerRange = FPItemRange<const GrProcessorSet, TextureSamplerIter>;

    // Sentinel type for range-for using Iter.
    class EndIter {};
    // Sentinel type for range-for using FPItemIter.
    class FPItemEndIter {};

protected:
    enum OptimizationFlags : uint32_t {
        kNone_OptimizationFlags,
        kCompatibleWithCoverageAsAlpha_OptimizationFlag = 0x1,
        kPreservesOpaqueInput_OptimizationFlag = 0x2,
        kConstantOutputForConstantInput_OptimizationFlag = 0x4,
        kAll_OptimizationFlags = kCompatibleWithCoverageAsAlpha_OptimizationFlag |
                                 kPreservesOpaqueInput_OptimizationFlag |
                                 kConstantOutputForConstantInput_OptimizationFlag
    };
    GR_DECL_BITFIELD_OPS_FRIENDS(OptimizationFlags)

    /**
     * Can be used as a helper to decide which fragment processor OptimizationFlags should be set.
     * This assumes that the subclass output color will be a modulation of the input color with a
     * value read from a texture of the passed color type and that the texture contains
     * premultiplied color or alpha values that are in range.
     *
     * Since there are multiple ways in which a sampler may have its coordinates clamped or wrapped,
     * callers must determine on their own if the sampling uses a decal strategy in any way, in
     * which case the texture may become transparent regardless of the color type.
     */
    static OptimizationFlags ModulateForSamplerOptFlags(SkAlphaType alphaType, bool samplingDecal) {
        if (samplingDecal) {
            return kCompatibleWithCoverageAsAlpha_OptimizationFlag;
        } else {
            return ModulateForClampedSamplerOptFlags(alphaType);
        }
    }

    // As above, but callers should somehow ensure or assert their sampler still uses clamping
    static OptimizationFlags ModulateForClampedSamplerOptFlags(SkAlphaType alphaType) {
        if (alphaType == kOpaque_SkAlphaType) {
            return kCompatibleWithCoverageAsAlpha_OptimizationFlag |
                   kPreservesOpaqueInput_OptimizationFlag;
        } else {
            return kCompatibleWithCoverageAsAlpha_OptimizationFlag;
        }
    }

    GrFragmentProcessor(ClassID classID, OptimizationFlags optimizationFlags)
            : INHERITED(classID), fFlags(optimizationFlags) {
        SkASSERT((optimizationFlags & ~kAll_OptimizationFlags) == 0);
    }

    OptimizationFlags optimizationFlags() const {
        return static_cast<OptimizationFlags>(kAll_OptimizationFlags & fFlags);
    }

    /** Useful when you can't call fp->optimizationFlags() on a base class object from a subclass.*/
    static OptimizationFlags ProcessorOptimizationFlags(const GrFragmentProcessor* fp) {
        return fp->optimizationFlags();
    }

    /**
     * This allows one subclass to access another subclass's implementation of
     * constantOutputForConstantInput. It must only be called when
     * hasConstantOutputForConstantInput() is known to be true.
     */
    static SkPMColor4f ConstantOutputForConstantInput(const GrFragmentProcessor& fp,
                                                      const SkPMColor4f& input) {
        SkASSERT(fp.hasConstantOutputForConstantInput());
        return fp.constantOutputForConstantInput(input);
    }

    /**
     * Fragment Processor subclasses call this from their constructor to register coordinate
     * transformations. Coord transforms provide a mechanism for a processor to receive coordinates
     * in their FS code. The matrix expresses a transformation from local space. For a given
     * fragment the matrix will be applied to the local coordinate that maps to the fragment.
     *
     * When the transformation has perspective, the transformed coordinates will have
     * 3 components. Otherwise they'll have 2.
     *
     * This must only be called from the constructor because GrProcessors are immutable. The
     * processor subclass manages the lifetime of the transformations (this function only stores a
     * pointer). The GrCoordTransform is typically a member field of the GrProcessor subclass.
     *
     * A processor subclass that has multiple methods of construction should always add its coord
     * transforms in a consistent order. The non-virtual implementation of isEqual() automatically
     * compares transforms and will assume they line up across the two processor instances.
     */
    void addCoordTransform(GrCoordTransform*);

    /**
     * FragmentProcessor subclasses call this from their constructor to register any child
     * FragmentProcessors they have. This must be called AFTER all texture accesses and coord
     * transforms have been added.
     * This is for processors whose shader code will be composed of nested processors whose output
     * colors will be combined somehow to produce its output color.  Registering these child
     * processors will allow the ProgramBuilder to automatically handle their transformed coords and
     * texture accesses and mangle their uniform and output color names.
     *
     * Depending on the 2nd and 3rd parameters, this corresponds to the following SkSL sample calls:
     *  - sample(child): Keep default arguments
     *  - sample(child, matrix): Provide approprate SampleMatrix matching SkSL
     *  - sample(child, float2): SampleMatrix() and 'true', or use 'registerExplicitlySampledChild'
     *  - sample(child, matrix)+sample(child, float2): Appropriate SampleMatrix and 'true'
     */
    int registerChild(std::unique_ptr<GrFragmentProcessor> child,
                      SkSL::SampleMatrix sampleMatrix = SkSL::SampleMatrix(),
                      bool explicitlySampled = false);

    /**
     * A helper for use when the child is only invoked with sample(float2), and not sample()
     * or sample(matrix).
     */
    int registerExplicitlySampledChild(std::unique_ptr<GrFragmentProcessor> child) {
        return this->registerChild(std::move(child), SkSL::SampleMatrix(), true);
    }

    /**
     * This method takes an existing fragment processor, clones it, registers it as a child of this
     * fragment processor, and returns its child index. It also takes care of any boilerplate in the
     * cloning process.
     */
    int cloneAndRegisterChildProcessor(const GrFragmentProcessor& fp);

    /**
     * This method takes an existing fragment processor, clones all of its children, and registers
     * the clones as children of this fragment processor.
     */
    void cloneAndRegisterAllChildProcessors(const GrFragmentProcessor& src);

    void setTextureSamplerCnt(int cnt) {
        SkASSERT(cnt >= 0);
        fTextureSamplerCnt = cnt;
    }

    // FP implementations must call this function if their matching GrGLSLFragmentProcessor's
    // emitCode() function uses the EmitArgs::fSampleCoord variable in generated SkSL.
    void setUsesSampleCoordsDirectly() {
        fFlags |= kUsesSampleCoordsDirectly_Flag;
    }

    /**
     * Helper for implementing onTextureSampler(). E.g.:
     * return IthTexureSampler(i, fMyFirstSampler, fMySecondSampler, fMyThirdSampler);
     */
    template <typename... Args>
    static const TextureSampler& IthTextureSampler(int i, const TextureSampler& samp0,
                                                   const Args&... samps) {
        return (0 == i) ? samp0 : IthTextureSampler(i - 1, samps...);
    }
    inline static const TextureSampler& IthTextureSampler(int i);

private:
    // Implementation details of Iter and CIter.
    template <typename> class IterBase;

    virtual SkPMColor4f constantOutputForConstantInput(const SkPMColor4f& /* inputColor */) const {
        SK_ABORT("Subclass must override this if advertising this optimization.");
    }

    /** Returns a new instance of the appropriate *GL* implementation class
        for the given GrFragmentProcessor; caller is responsible for deleting
        the object. */
    virtual GrGLSLFragmentProcessor* onCreateGLSLInstance() const = 0;

    /** Implemented using GLFragmentProcessor::GenKey as described in this class's comment. */
    virtual void onGetGLSLProcessorKey(const GrShaderCaps&, GrProcessorKeyBuilder*) const = 0;

    /**
     * Subclass implements this to support isEqual(). It will only be called if it is known that
     * the two processors are of the same subclass (i.e. they return the same object from
     * getFactory()). The processor subclass should not compare its coord transforms as that will
     * be performed automatically in the non-virtual isEqual().
     */
    virtual bool onIsEqual(const GrFragmentProcessor&) const = 0;

    virtual const TextureSampler& onTextureSampler(int) const { return IthTextureSampler(0); }

    bool hasSameTransforms(const GrFragmentProcessor&) const;

    void setSampleMatrix(SkSL::SampleMatrix matrix);

    enum PrivateFlags {
        kFirstPrivateFlag = kAll_OptimizationFlags + 1,

        // Propagate up the FP tree to the root
        kHasCoordTransforms_Flag = kFirstPrivateFlag,
        kUsesSampleCoordsIndirectly_Flag = kFirstPrivateFlag << 1,

        // Does not propagate at all
        kUsesSampleCoordsDirectly_Flag = kFirstPrivateFlag << 2,

        // Propagates down the FP to all its leaves
        kSampledWithExplicitCoords_Flag = kFirstPrivateFlag << 3,
        kNetTransformHasPerspective_Flag = kFirstPrivateFlag << 4,
    };
    void addAndPushFlagToChildren(PrivateFlags flag);

    uint32_t fFlags = 0;

    int fTextureSamplerCnt = 0;

    SkSTArray<4, GrCoordTransform*, true> fCoordTransforms;

    SkSTArray<1, std::unique_ptr<GrFragmentProcessor>, true> fChildProcessors;
    const GrFragmentProcessor* fParent = nullptr;
    SkSL::SampleMatrix fMatrix;

    typedef GrProcessor INHERITED;
};

/**
 * Used to represent a texture that is required by a GrFragmentProcessor. It holds a GrTextureProxy
 * along with an associated GrSamplerState. TextureSamplers don't perform any coord manipulation to
 * account for texture origin.
 */
class GrFragmentProcessor::TextureSampler {
public:
    TextureSampler() = default;

    /**
     * This copy constructor is used by GrFragmentProcessor::clone() implementations.
     */
    explicit TextureSampler(const TextureSampler&) = default;

    TextureSampler(GrSurfaceProxyView, GrSamplerState = {});

    TextureSampler(TextureSampler&&) = default;
    TextureSampler& operator=(TextureSampler&&) = default;
    TextureSampler& operator=(const TextureSampler&) = delete;

    bool operator==(const TextureSampler& that) const {
        return fView == that.fView && fSamplerState == that.fSamplerState;
    }

    bool operator!=(const TextureSampler& other) const { return !(*this == other); }

    SkDEBUGCODE(bool isInstantiated() const { return this->proxy()->isInstantiated(); })

    // 'peekTexture' should only ever be called after a successful 'instantiate' call
    GrTexture* peekTexture() const {
        SkASSERT(this->proxy()->isInstantiated());
        return this->proxy()->peekTexture();
    }

    const GrSurfaceProxyView& view() const { return fView; }
    GrSamplerState samplerState() const { return fSamplerState; }

    bool isInitialized() const { return SkToBool(this->proxy()); }

    GrSurfaceProxy* proxy() const { return fView.proxy(); }

#if GR_TEST_UTILS
    void set(GrSurfaceProxyView, GrSamplerState);
#endif

private:
    GrSurfaceProxyView    fView;
    GrSamplerState        fSamplerState;
};

//////////////////////////////////////////////////////////////////////////////

const GrFragmentProcessor::TextureSampler& GrFragmentProcessor::IthTextureSampler(int i) {
    SK_ABORT("Illegal texture sampler index");
    static const TextureSampler kBogus;
    return kBogus;
}

GR_MAKE_BITFIELD_OPS(GrFragmentProcessor::OptimizationFlags)

//////////////////////////////////////////////////////////////////////////////

template <typename FP> class GrFragmentProcessor::IterBase {
public:
    FP& operator*() const { return *fFPStack.back(); }
    FP* operator->() const { return fFPStack.back(); }
    operator bool() const { return !fFPStack.empty(); }
    bool operator!=(const EndIter&) { return (bool)*this; }

    // Hopefully this does not actually get called because of RVO.
    IterBase(const IterBase&) = default;

    // Because each iterator carries a stack we want to avoid copies.
    IterBase& operator=(const IterBase&) = delete;

protected:
    void increment();

    IterBase() = default;
    explicit IterBase(FP& fp) { fFPStack.push_back(&fp); }

    SkSTArray<4, FP*, true> fFPStack;
};

template <typename FP> void GrFragmentProcessor::IterBase<FP>::increment() {
    SkASSERT(!fFPStack.empty());
    FP* back = fFPStack.back();
    fFPStack.pop_back();
    for (int i = back->numChildProcessors() - 1; i >= 0; --i) {
        fFPStack.push_back(&back->childProcessor(i));
    }
}

//////////////////////////////////////////////////////////////////////////////

class GrFragmentProcessor::Iter : public IterBase<GrFragmentProcessor> {
public:
    explicit Iter(GrFragmentProcessor& fp) : IterBase(fp) {}
    Iter& operator++() {
        this->increment();
        return *this;
    }
};

//////////////////////////////////////////////////////////////////////////////

class GrFragmentProcessor::CIter : public IterBase<const GrFragmentProcessor> {
public:
    explicit CIter(const GrFragmentProcessor& fp) : IterBase(fp) {}
    explicit CIter(const GrPaint&);
    explicit CIter(const GrProcessorSet&);
    explicit CIter(const GrPipeline&);
    CIter& operator++() {
        this->increment();
        return *this;
    }
};

//////////////////////////////////////////////////////////////////////////////

template <typename Src> class GrFragmentProcessor::CIterRange {
public:
    explicit CIterRange(const Src& t) : fT(t) {}
    CIter begin() const { return CIter(fT); }
    EndIter end() const { return EndIter(); }

private:
    const Src& fT;
};

//////////////////////////////////////////////////////////////////////////////

template <typename Item, GrFragmentProcessor::CountFn Count, GrFragmentProcessor::GetFn<Item> Get>
class GrFragmentProcessor::FPItemIter {
public:
    template <typename Src> explicit FPItemIter(Src& s);

    std::pair<Item&, const GrFragmentProcessor&> operator*() const {
        return {(*fFPIter.*Get)(fIndex), *fFPIter};
    }
    FPItemIter& operator++();
    operator bool() const { return fFPIter; }
    bool operator!=(const FPItemEndIter&) { return (bool)*this; }

    FPItemIter(const FPItemIter&) = delete;
    FPItemIter& operator=(const FPItemIter&) = delete;

private:
    typename std::conditional<std::is_const<Item>::value, CIter, Iter>::type fFPIter;
    int fIndex;
};

template <typename Item, GrFragmentProcessor::CountFn Count, GrFragmentProcessor::GetFn<Item> Get>
template <typename Src>
GrFragmentProcessor::FPItemIter<Item, Count, Get>::FPItemIter(Src& s) : fFPIter(s), fIndex(-1) {
    if (fFPIter) {
        ++*this;
    }
}

template <typename Item, GrFragmentProcessor::CountFn Count, GrFragmentProcessor::GetFn<Item> Get>
GrFragmentProcessor::FPItemIter<Item, Count, Get>&
GrFragmentProcessor::FPItemIter<Item, Count, Get>::operator++() {
    ++fIndex;
    if (fIndex < ((*fFPIter).*Count)()) {
        return *this;
    }
    fIndex = 0;
    do {} while (++fFPIter && !((*fFPIter).*Count)());
    return *this;
}

//////////////////////////////////////////////////////////////////////////////

template <typename Src, typename ItemIter> class GrFragmentProcessor::FPItemRange {
public:
    FPItemRange(Src& src) : fSrc(src) {}
    ItemIter begin() const { return ItemIter(fSrc); }
    FPItemEndIter end() const { return FPItemEndIter(); }

private:
    Src& fSrc;
};

/**
 * Some fragment-processor creation methods have preconditions that might not be satisfied by the
 * calling code. Those methods can return a `GrFPResult` from their factory methods. If creation
 * succeeds, the new fragment processor is created and `success` is true. If a precondition is not
 * met, `success` is set to false and the input FP is returned unchanged.
 */
using GrFPResult = std::tuple<bool /*success*/, std::unique_ptr<GrFragmentProcessor>>;
static inline GrFPResult GrFPFailure(std::unique_ptr<GrFragmentProcessor> fp) {
    return {false, std::move(fp)};
}
static inline GrFPResult GrFPSuccess(std::unique_ptr<GrFragmentProcessor> fp) {
    return {true, std::move(fp)};
}

#endif
