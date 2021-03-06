// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the OpenColorIO Project.

#include <cstring>
#include <sstream>

#include <OpenColorIO/OpenColorIO.h>

#include "GpuShaderUtils.h"
#include "ops/fixedfunction/FixedFunctionOpCPU.h"
#include "ops/fixedfunction/FixedFunctionOpGPU.h"
#include "ops/fixedfunction/FixedFunctionOp.h"
#include "transforms/FixedFunctionTransform.h"

namespace OCIO_NAMESPACE
{

namespace
{

class FixedFunctionOp;
typedef OCIO_SHARED_PTR<FixedFunctionOp> FixedFunctionOpRcPtr;
typedef OCIO_SHARED_PTR<const FixedFunctionOp> ConstFixedFunctionOpRcPtr;

class FixedFunctionOp : public Op
{
public:
    FixedFunctionOp() = delete;
    FixedFunctionOp(const FixedFunctionOp &) = delete;
    explicit FixedFunctionOp(FixedFunctionOpDataRcPtr & func);

    virtual ~FixedFunctionOp();

    OpRcPtr clone() const override;

    std::string getInfo() const override;

    bool isIdentity() const override;
    bool isSameType(ConstOpRcPtr & op) const override;
    bool isInverse(ConstOpRcPtr & op) const override;
    bool canCombineWith(ConstOpRcPtr & op) const override;
    void combineWith(OpRcPtrVec & ops, ConstOpRcPtr & secondOp) const override;

    void finalize(OptimizationFlags oFlags) override;

    ConstOpCPURcPtr getCPUOp() const override;

    void extractGpuShaderInfo(GpuShaderDescRcPtr & shaderDesc) const override;

protected:
    ConstFixedFunctionOpDataRcPtr fnData() const { return DynamicPtrCast<const FixedFunctionOpData>(data()); }
    FixedFunctionOpDataRcPtr fnData() { return DynamicPtrCast<FixedFunctionOpData>(data()); }
};


FixedFunctionOp::FixedFunctionOp(FixedFunctionOpDataRcPtr & func)
    :   Op()
{
    data() = func;
}

OpRcPtr FixedFunctionOp::clone() const
{
    FixedFunctionOpDataRcPtr f = fnData()->clone();
    return std::make_shared<FixedFunctionOp>(f);
}

FixedFunctionOp::~FixedFunctionOp()
{
}

std::string FixedFunctionOp::getInfo() const
{
    return "<FixedFunctionOp>";
}

bool FixedFunctionOp::isIdentity() const
{
    return fnData()->isIdentity();
}

bool FixedFunctionOp::isSameType(ConstOpRcPtr & op) const
{
    ConstFixedFunctionOpRcPtr typedRcPtr = DynamicPtrCast<const FixedFunctionOp>(op);
    return (bool)typedRcPtr;
}

bool FixedFunctionOp::isInverse(ConstOpRcPtr & op) const
{
    ConstFixedFunctionOpRcPtr typedRcPtr = DynamicPtrCast<const FixedFunctionOp>(op);
    if (!typedRcPtr) return false;

    ConstFixedFunctionOpDataRcPtr fnOpData = typedRcPtr->fnData();
    return fnData()->isInverse(fnOpData);
}

bool FixedFunctionOp::canCombineWith(ConstOpRcPtr & /*op*/) const
{
    return false;
}

void FixedFunctionOp::combineWith(OpRcPtrVec & /*ops*/, ConstOpRcPtr & secondOp) const
{
    if (!canCombineWith(secondOp))
    {
        throw Exception("FixedFunctionOp: canCombineWith must be checked "
                        "before calling combineWith.");
    }
}

void FixedFunctionOp::finalize(OptimizationFlags /*oFlags*/)
{
    fnData()->finalize();

    // Create the cacheID
    std::ostringstream cacheIDStream;
    cacheIDStream << "<FixedFunctionOp ";
    cacheIDStream << fnData()->getCacheID() << " ";
    cacheIDStream << ">";

    m_cacheID = cacheIDStream.str();
}

ConstOpCPURcPtr FixedFunctionOp::getCPUOp() const
{
    ConstFixedFunctionOpDataRcPtr data = fnData();
    return GetFixedFunctionCPURenderer(data);
}

void FixedFunctionOp::extractGpuShaderInfo(GpuShaderDescRcPtr & shaderDesc) const
{
    GpuShaderText ss(shaderDesc->getLanguage());
    ss.indent();

    ConstFixedFunctionOpDataRcPtr fnOpData = fnData();
    GetFixedFunctionGPUShaderProgram(ss, fnOpData);

    ss.dedent();

    shaderDesc->addToFunctionShaderCode(ss.string().c_str());
}


}  // Anon namespace




///////////////////////////////////////////////////////////////////////////


void CreateFixedFunctionOp(OpRcPtrVec & ops, 
                           const FixedFunctionOpData::Params & params,
                           FixedFunctionOpData::Style style)
{
    FixedFunctionOpDataRcPtr funcData = std::make_shared<FixedFunctionOpData>(params, style);
    CreateFixedFunctionOp(ops, funcData, TRANSFORM_DIR_FORWARD);
}

void CreateFixedFunctionOp(OpRcPtrVec & ops, 
                           FixedFunctionOpDataRcPtr & funcData,
                           TransformDirection direction)
{
    auto func = funcData;
    if (direction == TRANSFORM_DIR_INVERSE)
    {
        func = func->inverse();
    }

    ops.push_back(std::make_shared<FixedFunctionOp>(func));
}

///////////////////////////////////////////////////////////////////////////

void CreateFixedFunctionTransform(GroupTransformRcPtr & group, ConstOpRcPtr & op)
{
    auto ff = DynamicPtrCast<const FixedFunctionOp>(op);
    if (!ff)
    {
        throw Exception("CreateFixedFunctionTransform: op has to be a FixedFunctionOp");
    }
    auto ffData = DynamicPtrCast<const FixedFunctionOpData>(op->data());
    auto ffTransform = FixedFunctionTransform::Create();
    auto & data = dynamic_cast<FixedFunctionTransformImpl *>(ffTransform.get())->data();
    data = *ffData;

    group->appendTransform(ffTransform);
}

void BuildFixedFunctionOp(OpRcPtrVec & ops,
                          const Config & /*config*/,
                          const ConstContextRcPtr & /*context*/,
                          const FixedFunctionTransform & transform,
                          TransformDirection dir)
{
    const auto & data = dynamic_cast<const FixedFunctionTransformImpl &>(transform).data();
    data.validate();

    auto funcData = data.clone();
    CreateFixedFunctionOp(ops, funcData, dir);
}


} // namespace OCIO_NAMESPACE

