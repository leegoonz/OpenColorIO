// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the OpenColorIO Project.

#include <cstdio>
#include <cstring>
#include <iterator>
#include <algorithm>

#include <OpenColorIO/OpenColorIO.h>

#include "ops/lut1d/Lut1DOp.h"
#include "ops/lut3d/Lut3DOp.h"
#include "ParseUtils.h"
#include "pystring/pystring.h"
#include "transforms/FileTransform.h"

/*

Iridas itx format
LUT_3D_SIZE M

#LUT_3D_SIZE M
#where M is the size of the texture
#a 3D texture has the size M x M x M
#e.g. LUT_3D_SIZE 16 creates a 16 x 16 x 16 3D texture

#for 1D textures, the data is simply a list of floating point values,
#three per line, in RGB order
#for 3D textures, the data is also RGB, and ordered in such a way
#that the red coordinate changes fastest, then the green coordinate,
#and finally, the blue coordinate changes slowest:
0.0 0.0 0.0
1.0 0.0 0.0
0.0 1.0 0.0
1.0 1.0 0.0
0.0 0.0 1.0
1.0 0.0 1.0
0.0 1.0 1.0
1.0 1.0 1.0
*/


namespace OCIO_NAMESPACE
{
namespace
{
class LocalCachedFile : public CachedFile
{
public:
    LocalCachedFile() = default;
    ~LocalCachedFile() = default;

    Lut3DOpDataRcPtr lut3D;
};

typedef OCIO_SHARED_PTR<LocalCachedFile> LocalCachedFileRcPtr;



class LocalFileFormat : public FileFormat
{
public:

    LocalFileFormat() = default;
    ~LocalFileFormat() = default;

    void getFormatInfo(FormatInfoVec & formatInfoVec) const override;

    CachedFileRcPtr read(
        std::istream & istream,
        const std::string & fileName) const override;

    void bake(const Baker & baker,
                const std::string & formatName,
                std::ostream & ostream) const override;

    void buildFileOps(OpRcPtrVec & ops,
                        const Config & config,
                        const ConstContextRcPtr & context,
                        CachedFileRcPtr untypedCachedFile,
                        const FileTransform & fileTransform,
                        TransformDirection dir) const override;

private:
    static void ThrowErrorMessage(const std::string & error,
        const std::string & fileName,
        int line,
        const std::string & lineContent);
};

void LocalFileFormat::ThrowErrorMessage(const std::string & error,
    const std::string & fileName,
    int line,
    const std::string & lineContent)
{
    std::ostringstream os;
    os << "Error parsing Iridas .itx file (";
    os << fileName;
    os << ").  ";
    if (-1 != line)
    {
        os << "At line (" << line << "): '";
        os << lineContent << "'.  ";
    }
    os << error;

    throw Exception(os.str().c_str());
}

void LocalFileFormat::getFormatInfo(FormatInfoVec & formatInfoVec) const
{
    FormatInfo info;
    info.name = "iridas_itx";
    info.extension = "itx";
    info.capabilities = (FORMAT_CAPABILITY_READ | FORMAT_CAPABILITY_BAKE);
    formatInfoVec.push_back(info);
}

CachedFileRcPtr
LocalFileFormat::read(
    std::istream & istream,
    const std::string & fileName) const
{
    // this shouldn't happen
    if(!istream)
    {
        throw Exception ("File stream empty when trying to read Iridas .itx LUT");
    }

    // Parse the file
    std::vector<float> raw;

    int size3d = 0;
    bool in3d = false;

    {
        std::string line;
        StringVec parts;
        std::vector<float> tmpfloats;
        int lineNumber = 0;

        while(nextline(istream, line))
        {
            ++lineNumber;
            // All lines starting with '#' are comments
            if(pystring::startswith(line,"#")) continue;

            // Strip, lowercase, and split the line
            pystring::split(pystring::lower(pystring::strip(line)), parts);
            if(parts.empty()) continue;

            if(pystring::lower(parts[0]) == "lut_3d_size")
            {
                int size = 0;

                if(parts.size() != 2 
                    || !StringToInt( &size, parts[1].c_str()))
                {
                    ThrowErrorMessage(
                        "Malformed LUT_3D_SIZE tag.",
                        fileName,
                        lineNumber,
                        line);
                }
                size3d = size;

                raw.reserve(3*size3d * size3d * size3d);
                in3d = true;
            }
            else if(in3d)
            {
                // It must be a float triple!

                if(!StringVecToFloatVec(tmpfloats, parts)
                    || tmpfloats.size() != 3)
                {
                    ThrowErrorMessage(
                        "Malformed color triples specified.",
                        fileName,
                        lineNumber,
                        line);
                }

                for(int i=0; i<3; ++i)
                {
                    raw.push_back(tmpfloats[i]);
                }
            }
        }
    }

    // Interpret the parsed data, validate LUT sizes
    LocalCachedFileRcPtr cachedFile 
        = LocalCachedFileRcPtr(new LocalCachedFile());

    if(in3d)
    {
        if(size3d * size3d * size3d
            != static_cast<int>(raw.size()/3))
        {
            std::ostringstream os;
            os << "Incorrect number of 3D LUT entries. ";
            os << "Found " << raw.size() / 3 << ", expected ";
            os << size3d * size3d * size3d << ".";
            ThrowErrorMessage(
                os.str().c_str(),
                fileName, -1, "");
        }

        // Reformat 3D data
        cachedFile->lut3D = std::make_shared<Lut3DOpData>(size3d);
        cachedFile->lut3D->setFileOutputBitDepth(BIT_DEPTH_F32);
        cachedFile->lut3D->setArrayFromRedFastestOrder(raw);
    }
    else
    {
        ThrowErrorMessage(
            "No 3D LUT found.",
            fileName, -1, "");
    }

    return cachedFile;
}

void LocalFileFormat::bake(const Baker & baker,
                            const std::string & formatName,
                            std::ostream & ostream) const
{
    int DEFAULT_CUBE_SIZE = 64;

    if(formatName != "iridas_itx")
    {
        std::ostringstream os;
        os << "Unknown 3dl format name, '";
        os << formatName << "'.";
        throw Exception(os.str().c_str());
    }

    ConstConfigRcPtr config = baker.getConfig();

    int cubeSize = baker.getCubeSize();
    if(cubeSize==-1) cubeSize = DEFAULT_CUBE_SIZE;
    cubeSize = std::max(2, cubeSize); // smallest cube is 2x2x2

    std::vector<float> cubeData;
    cubeData.resize(cubeSize*cubeSize*cubeSize*3);
    GenerateIdentityLut3D(&cubeData[0], cubeSize, 3, LUT3DORDER_FAST_RED);
    PackedImageDesc cubeImg(&cubeData[0], cubeSize*cubeSize*cubeSize, 1, 3);

    // Apply our conversion from the input space to the output space.
    ConstProcessorRcPtr inputToTarget;
    std::string looks = baker.getLooks();
    if (!looks.empty())
    {
        LookTransformRcPtr transform = LookTransform::Create();
        transform->setLooks(looks.c_str());
        transform->setSrc(baker.getInputSpace());
        transform->setDst(baker.getTargetSpace());
        inputToTarget = config->getProcessor(transform,
            TRANSFORM_DIR_FORWARD);
    }
    else
    {
        inputToTarget = config->getProcessor(baker.getInputSpace(),
            baker.getTargetSpace());
    }
    ConstCPUProcessorRcPtr cpu = inputToTarget->getDefaultCPUProcessor();
    cpu->apply(cubeImg);

    // Write out the file.
    // For for maximum compatibility with other apps, we will
    // not utilize the shaper or output any metadata

    ostream << "LUT_3D_SIZE " << cubeSize << "\n";
    if(cubeSize < 2)
    {
        throw Exception("Internal cube size exception.");
    }

    // Set to a fixed 6 decimal precision
    ostream.setf(std::ios::fixed, std::ios::floatfield);
    ostream.precision(6);
    for(int i=0; i<cubeSize*cubeSize*cubeSize; ++i)
    {
        float r = cubeData[3*i+0];
        float g = cubeData[3*i+1];
        float b = cubeData[3*i+2];
        ostream << r << " " << g << " " << b << "\n";
    }
    ostream << "\n";
}


void
LocalFileFormat::buildFileOps(OpRcPtrVec & ops,
                                const Config & /*config*/,
                                const ConstContextRcPtr & /*context*/,
                                CachedFileRcPtr untypedCachedFile,
                                const FileTransform & fileTransform,
                                TransformDirection dir) const
{
    LocalCachedFileRcPtr cachedFile = DynamicPtrCast<LocalCachedFile>(untypedCachedFile);

    // This should never happen.
    if(!cachedFile)
    {
        std::ostringstream os;
        os << "Cannot build Iridas .itx Op. Invalid cache type.";
        throw Exception(os.str().c_str());
    }

    TransformDirection newDir = CombineTransformDirections(dir,
        fileTransform.getDirection());
    if(newDir == TRANSFORM_DIR_UNKNOWN)
    {
        std::ostringstream os;
        os << "Cannot build file format transform,";
        os << " unspecified transform direction.";
        throw Exception(os.str().c_str());
    }

    if (cachedFile->lut3D)
    {
        cachedFile->lut3D->setInterpolation(fileTransform.getInterpolation());
        CreateLut3DOp(ops, cachedFile->lut3D, newDir);
    }
}
}

FileFormat * CreateFileFormatIridasItx()
{
    return new LocalFileFormat();
}

} // namespace OCIO_NAMESPACE

