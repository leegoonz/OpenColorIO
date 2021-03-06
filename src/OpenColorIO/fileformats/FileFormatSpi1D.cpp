// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the OpenColorIO Project.

#include <cstdio>
#include <sstream>

#include <OpenColorIO/OpenColorIO.h>

#include "ops/lut1d/Lut1DOp.h"
#include "ops/matrix/MatrixOp.h"
#include "ParseUtils.h"
#include "Platform.h"
#include "pystring/pystring.h"
#include "transforms/FileTransform.h"

/*
Version 1
From -7.5 3.7555555555555555
Components 1
Length 4096
{
        0.031525943963232252
        0.045645604561056156
	...
}

*/

namespace OCIO_NAMESPACE
{
////////////////////////////////////////////////////////////////

namespace
{
class LocalCachedFile : public CachedFile
{
public:
    LocalCachedFile() = default;
    ~LocalCachedFile() = default;

    Lut1DOpDataRcPtr lut;
    float from_min = 0.0f;
    float from_max = 1.0f;
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

void LocalFileFormat::getFormatInfo(FormatInfoVec & formatInfoVec) const
{
    FormatInfo info;
    info.name = "spi1d";
    info.extension = "spi1d";
    info.capabilities = FORMAT_CAPABILITY_READ;
    formatInfoVec.push_back(info);
}

// Try and load the format.
// Raise an exception if it can't be loaded.

CachedFileRcPtr LocalFileFormat::read(
    std::istream & istream,
    const std::string & fileName ) const
{
    // Parse Header Info.
    int lut_size = -1;
    float from_min = 0.0;
    float from_max = 1.0;
    int version = -1;
    int components = -1;

    const int MAX_LINE_SIZE = 4096;
    char lineBuffer[MAX_LINE_SIZE];
    int currentLine = 0;

    // PARSE HEADER INFO
    {
        std::string headerLine("");
        do
        {
            istream.getline(lineBuffer, MAX_LINE_SIZE);
            ++currentLine;
            headerLine = std::string(lineBuffer);

            if(pystring::startswith(headerLine, "Version"))
            {
                // " " in format means any number of spaces (white space,
                // new line, tab) including 0 of them.
                // "Version1" is valid.
                if (sscanf(lineBuffer, "Version %d", &version) != 1)
                {
                    ThrowErrorMessage("Invalid 'Version' Tag.",
                                        fileName, currentLine, headerLine);
                }
                else if (version != 1)
                {
                    ThrowErrorMessage("Only format version 1 supported.",
                                        fileName, currentLine, headerLine);
                }
            }
            else if(pystring::startswith(headerLine, "From"))
            {
                if (sscanf(lineBuffer, "From %f %f", &from_min, &from_max) != 2)
                {
                    ThrowErrorMessage("Invalid 'From' Tag.",
                                        fileName, currentLine, headerLine);
                }
            }
            else if(pystring::startswith(headerLine, "Components"))
            {
                if (sscanf(lineBuffer, "Components %d", &components) != 1)
                {
                    ThrowErrorMessage("Invalid 'Components' Tag.",
                                        fileName, currentLine, headerLine);
                }
            }
            else if(pystring::startswith(headerLine, "Length"))
            {
                if (sscanf(lineBuffer, "Length %d", &lut_size) != 1)
                {
                    ThrowErrorMessage("Invalid 'Length' Tag.",
                                        fileName, currentLine, headerLine);
                }
            }
        }
        while (istream.good() && !pystring::startswith(headerLine,"{"));
    }

    if (version == -1)
    {
        ThrowErrorMessage("Could not find 'Version' Tag.",
                            fileName, -1, "");
    }
    if (lut_size == -1)
    {
        ThrowErrorMessage("Could not find 'Length' Tag.",
                            fileName, -1, "");
    }
    if (components == -1)
    {
        ThrowErrorMessage("Could not find 'Components' Tag.",
                            fileName, -1, "");
    }
    if (components < 0 || components>3)
    {
        ThrowErrorMessage("Components must be [1,2,3].",
                            fileName, -1, "");
    }

    Lut1DOpDataRcPtr lut1d = std::make_shared<Lut1DOpData>(lut_size);
    lut1d->setFileOutputBitDepth(BIT_DEPTH_F32);
    Array & lutArray = lut1d->getArray();
    unsigned long i = 0;
    {
        istream.getline(lineBuffer, MAX_LINE_SIZE);
        ++currentLine;

        int lineCount=0;

        std::vector<std::string> inputLUT;
        std::vector<float> values;

        while (istream.good())
        {
            const std::string line = pystring::strip(std::string(lineBuffer));
            if (Platform::Strcasecmp(line.c_str(), "}") == 0)
            {
                break;
            }

            if (line.length() != 0)
            {
                pystring::split(pystring::strip(lineBuffer), inputLUT);
                values.clear();
                if (!StringVecToFloatVec(values, inputLUT)
                    || components != (int)values.size())
                {
                    std::ostringstream os;
                    os << "Malformed LUT line. Expecting a ";
                    os << components << " components entry.";

                    ThrowErrorMessage("Malformed LUT line.",
                                        fileName, currentLine, line);
                }

                // If 1 component is specified, use x1 x1 x1.
                if (components == 1)
                {
                    lutArray[i]     = values[0];
                    lutArray[i + 1] = values[0];
                    lutArray[i + 2] = values[0];
                    i += 3;
                    ++lineCount;
                }
                // If 2 components are specified, use x1 x2 0.0.
                else if (components == 2)
                {
                    lutArray[i]     = values[0];
                    lutArray[i + 1] = values[1];
                    lutArray[i + 2] = 0.0f;
                    i += 3;
                    ++lineCount;
                }
                // If 3 component is specified, use x1 x2 x3.
                else if (components == 3)
                {
                    lutArray[i]     = values[0];
                    lutArray[i + 1] = values[1];
                    lutArray[i + 2] = values[2];
                    i += 3;
                    ++lineCount;
                }
                // No other case, components is in [1..3].
            }

            istream.getline(lineBuffer, MAX_LINE_SIZE);
            ++currentLine;
        }

        if (lineCount != lut_size)
        {
            ThrowErrorMessage("Not enough entries found.",
                                fileName, -1, "");
        }
    }

    LocalCachedFileRcPtr cachedFile = LocalCachedFileRcPtr(new LocalCachedFile());
    cachedFile->lut = lut1d;
    cachedFile->from_min = from_min;
    cachedFile->from_max = from_max;
    return cachedFile;
}

void LocalFileFormat::buildFileOps(OpRcPtrVec & ops,
                                    const Config & /*config*/,
                                    const ConstContextRcPtr & /*context*/,
                                    CachedFileRcPtr untypedCachedFile,
                                    const FileTransform& fileTransform,
                                    TransformDirection dir) const
{
    LocalCachedFileRcPtr cachedFile = DynamicPtrCast<LocalCachedFile>(untypedCachedFile);

    if(!cachedFile) // This should never happen.
    {
        std::ostringstream os;
        os << "Cannot build Spi1D Op. Invalid cache type.";
        throw Exception(os.str().c_str());
    }

    TransformDirection newDir = fileTransform.getDirection();
    newDir = CombineTransformDirections(dir, newDir);


    const double min[3] = { cachedFile->from_min,
                            cachedFile->from_min,
                            cachedFile->from_min };

    const double max[3] = { cachedFile->from_max,
                            cachedFile->from_max,
                            cachedFile->from_max };

    cachedFile->lut->setInterpolation(fileTransform.getInterpolation());

    if (newDir == TRANSFORM_DIR_FORWARD)
    {
        CreateMinMaxOp(ops, min, max, TRANSFORM_DIR_FORWARD);
        CreateLut1DOp(ops, cachedFile->lut, TRANSFORM_DIR_FORWARD);
    }
    else
    {
        CreateLut1DOp(ops, cachedFile->lut, TRANSFORM_DIR_INVERSE);
        CreateMinMaxOp(ops, min, max, TRANSFORM_DIR_INVERSE);
    }
}

void LocalFileFormat::ThrowErrorMessage(const std::string & error,
                                        const std::string & fileName,
                                        int line,
                                        const std::string & lineContent)
{
    std::ostringstream os;
    os << "Error parsing .spi1d file (";
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
}

FileFormat * CreateFileFormatSpi1D()
{
    return new LocalFileFormat();
}

} // namespace OCIO_NAMESPACE

