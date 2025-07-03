#ifndef __PROGTEST__
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cctype>
#include <climits>
#include <cstdint>
#include <cassert>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <optional>
#include <memory>
#include <stdexcept>
#include <set>
#include <map>
#include <queue>
#include <deque>
#include <stack>
#include <unordered_map>
#include <unordered_set>
#endif /* __PROGTEST__ */

using ui8 = uint8_t;
using ui32 = uint32_t;

class CLinker
{
public:
    CLinker() = default;
    ~CLinker() = default;

    CLinker(const CLinker &) = delete;
    CLinker & operator = ( const CLinker & ) = delete;

    CLinker & addFile ( const std::string & fileName );
    void linkOutput ( const std::string & fileName, const std::string & entryPoint );

private:
    // structure for exported functions
    struct ExportFun {
        std::string name;
        ui32 offset; // offset in the file
    };

    // structure for imported functions
    struct ImportFun {
        std::string name;
        std::vector<ui32> references; // references to functions in the file (place of use)
    };

    // structure for the file header
    struct Header {
        ui32 expCnt; // number of exports
        ui32 imCnt; // number of imports
        ui32 codeSz; // code size
    };

    // structure for function information
    struct FunInfo {
        size_t fileIdx; // index of the file where it is used
        ui32 offset; // offset in the file
        ui32 size; // size of the function in bytes
        ui32 offsetData; // code offset
    };

    // structure for the object file
    struct ObjFile {
        std::vector<ImportFun> imports;
        std::vector<ExportFun> exports;
        Header fileHeader;
        ui32 offsetData; // offset of the code in the file

    };

    void readFiles();
    void FunInfoMap();
    std::vector<ui8> loadCode(const FunInfo & functionIn);
    // breadth-first search to find used functions
    void bfs(const std::string & entryPoint, std::vector<std::string> & neededFunctions);

    std::vector<std::string> m_Files; // list of file names
    std::vector<ObjFile> m_ObjFiles; // list of object files
    std::unordered_map<std::string, std::shared_ptr<FunInfo>> m_FunctionInfoMap; // map for information about functions
};


// add file name to the list
CLinker & CLinker::addFile(const std::string & fileName) {
    m_Files.push_back(fileName);
    return *this;
}

// read object files
void CLinker::readFiles() {
    m_ObjFiles.clear();
    m_ObjFiles.reserve(m_Files.size());
    m_FunctionInfoMap.clear();

    // map to check for duplicate exports
    std::map<std::string, size_t> exportDup;

    for (size_t k = 0; k < m_Files.size(); ++k) {
        const auto & fileName = m_Files[k];
        std::ifstream file(fileName, std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Failed reading input file");
        }
        ObjFile objFile;

        // read the header
        if (!file.read(reinterpret_cast<char *>(&objFile.fileHeader.expCnt), sizeof(objFile.fileHeader.expCnt)) ||
            !file.read(reinterpret_cast<char *>(&objFile.fileHeader.imCnt), sizeof(objFile.fileHeader.imCnt)) ||
            !file.read(reinterpret_cast<char *>(&objFile.fileHeader.codeSz), sizeof(objFile.fileHeader.codeSz))) {
            file.close();
            throw std::runtime_error("Failed reading input file");
        }

        // read exports
        objFile.exports.reserve(objFile.fileHeader.expCnt);
        for (ui32 i = 0; i < objFile.fileHeader.expCnt; ++i) {

            ui8 nameLen;
            if (!file.read(reinterpret_cast<char *>(&nameLen), sizeof(nameLen))) {
                file.close();
                throw std::runtime_error("Failed reading input file");
            }

            std::string name(nameLen, '\0');
            if (!file.read(name.data(), nameLen)) {
                file.close();
                throw std::runtime_error("Failed reading input file");
            }

            ui32 offset;
            if (!file.read(reinterpret_cast<char *>(&offset), sizeof(offset))) {
                file.close();
                throw std::runtime_error("Failed reading input file");
            }

            if (exportDup.find(name) != exportDup.end()) {
                file.close();
                throw std::runtime_error("Duplicate symbol: " + name);
            }

            exportDup[name] = k;

            objFile.exports.push_back({name, offset});
        }

        // read imports
        objFile.imports.reserve(objFile.fileHeader.imCnt);
        for (ui32 i = 0; i < objFile.fileHeader.imCnt; ++i) {
            ui8 nameLen;

            if (!file.read(reinterpret_cast<char *>(&nameLen), sizeof(nameLen))) {
                file.close();
                throw std::runtime_error("Failed reading input file");
            }

            std::string name(nameLen, '\0');
            if (!file.read(name.data(), nameLen)) {
                file.close();
                throw std::runtime_error("Failed reading input file");
            }

            ui32 refCnt;
            if (!file.read(reinterpret_cast<char *>(&refCnt), sizeof(refCnt))) {
                file.close();
                throw std::runtime_error("Failed reading input file");
            }

            ImportFun importedFun;
            importedFun.name = std::move(name);
            importedFun.references.reserve(refCnt);
            for (ui32 j = 0; j < refCnt; ++j) {
                ui32 offset;
                if (!file.read(reinterpret_cast<char *>(&offset), sizeof(offset))) {
                    file.close();
                    throw std::runtime_error("Failed reading input file");
                }
                importedFun.references.push_back({offset});
            }

            objFile.imports.push_back(std::move(importedFun));
        }

        // skip code, save offset for future code loading
        objFile.offsetData = file.tellg();
        file.close();
        m_ObjFiles.push_back(std::move(objFile));
    }
}

// create a map of function information
void CLinker::FunInfoMap() {
    m_FunctionInfoMap.clear();
    for (size_t i = 0; i < m_ObjFiles.size(); ++i) {
        const auto & objFile = m_ObjFiles[i];

        for (const auto & exportF : objFile.exports) {
            ui32 nextOffset = objFile.fileHeader.codeSz;

            for (const auto & otherExportF : objFile.exports) {
                if (otherExportF.offset > exportF.offset && otherExportF.offset < nextOffset) {
                    nextOffset = otherExportF.offset;
                }
            }

            // size of the function is calculated as the difference between the next export offset and the current one
            ui32 funSize = nextOffset - exportF.offset;

            m_FunctionInfoMap[exportF.name] = std::make_shared<FunInfo>(FunInfo{i, exportF.offset, funSize, objFile.offsetData});
        }
    }
}

// load the code of the function seperated from the object file (if needed)
std::vector<ui8> CLinker::loadCode(const FunInfo & functionIn) {
    std::ifstream file(m_Files[functionIn.fileIdx], std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed reading input file");
    }
    // skip header, exports, and imports, go straight to the code
    file.seekg(functionIn.offset + functionIn.offsetData, std::ios::beg);

    std::vector<ui8> code(functionIn.size);
    const size_t chunkSize = 2048; // Read in 2KB chunks
    size_t bytesRead = 0;

    while (bytesRead < functionIn.size) {
        size_t toRead = std::min(chunkSize, functionIn.size - bytesRead);
        if (!file.read(reinterpret_cast<char *>(code.data() + bytesRead), toRead)) {
            file.close();
            throw std::runtime_error("Failed reading input file");
        }
        bytesRead += toRead;
    }
    file.close();
    return code;
}

// breadth-first search to find all functions that are needed
void CLinker::bfs(const std::string & entryPoint, std::vector<std::string> & neededFunctions) {

    // entrypoint does not exist
    if (m_FunctionInfoMap.find(entryPoint) == m_FunctionInfoMap.end()) {
        throw std::runtime_error("Undefined symbol " + entryPoint);
    }

    std::set<std::string> visited = { entryPoint };
    std::deque<std::string> q = { entryPoint };

    while (!q.empty()) {
        std::string currFun = std::move(q.front());
        q.pop_front();

        neededFunctions.push_back(currFun);

        const auto & funInfo = m_FunctionInfoMap[currFun];

        for (const auto & importedFunction : m_ObjFiles[funInfo->fileIdx].imports) {

            bool used = false;

            for (const auto & ref : importedFunction.references) {
                // is the function used in the current function (reference is in the range of the function)
                if (ref >= funInfo->offset && ref < funInfo->offset + funInfo->size) {
                    used = true;
                    break;
                }
            }

            if (used) {
                // if the function is used, check if it is defined
                if (m_FunctionInfoMap.find(importedFunction.name) == m_FunctionInfoMap.end()) {
                    throw std::runtime_error("Undefined symbol " + importedFunction.name);
                }

                if (visited.find(importedFunction.name) == visited.end()) {
                    q.push_back(importedFunction.name);
                    visited.insert(importedFunction.name);
                }
            }
        }
    }
}

// link the output file
void CLinker::linkOutput(const std::string & fileName, const std::string & entryPoint) {
    readFiles();
    FunInfoMap();

    // find all functions that are needed
    std::vector<std::string> neededFunctions;
    neededFunctions.reserve(m_FunctionInfoMap.size());
    bfs(entryPoint, neededFunctions);

    // sort the functions by name for local assert passing
    std::sort(neededFunctions.begin() + 1, neededFunctions.end());

    std::map<std::string, ui32> functionOffsets;

    ui32 currentOffset = 0;
    for (const auto & fun : neededFunctions) {
        functionOffsets[fun] = currentOffset;
        currentOffset += m_FunctionInfoMap[fun]->size;
    }

    std::ofstream outFile(fileName, std::ios::binary);
    if (!outFile.is_open()) {
        throw std::runtime_error("Failed writing output file");
    }

    // create output file
    for (const auto & fun : neededFunctions) {
        const auto & funInfo = m_FunctionInfoMap[fun];
        size_t fileIdx = funInfo->fileIdx;

        // read the code only if needed
        std::vector<ui8> functionBody = loadCode(*funInfo);


        for (const auto & importFun : m_ObjFiles[fileIdx].imports) {
            // if the function is imported and is used, we add it to output
            // and change all calls of this function with the address of the function
            if (functionOffsets.find(importFun.name) != functionOffsets.end()) {
                ui32 targetAddress = functionOffsets[importFun.name];

                for (const auto & ref : importFun.references) {
                    if (ref >= funInfo->offset && ref < funInfo->offset + funInfo->size) {
                        ui32 relativeOffset = ref - funInfo->offset;

                        memcpy(&functionBody[relativeOffset], &targetAddress, sizeof(targetAddress));
                    }
                }
            }
        }
        outFile.write(reinterpret_cast<const char *>(functionBody.data()), functionBody.size());
    }

    // check if the output file was written successfully
    if (!outFile) {
        outFile.close();
        throw std::runtime_error("Failed writing output file");
    }
}






using namespace std;

bool identicalFiles(const char *file1, const char *file2) {
    ifstream f01(file1), f02(file2);
    char ch;
    cout << file1 << ":\t\t";
    while (f01.get(ch)) {
        //cout << getBytes(ch, 8) << " ";
        // cout << (int) (ch >= 0 ? ch : (256 + ch)) << "\t";
    }
    cout << endl;
    cout << file2 << ":\t";
    while (f02.get(ch)) {
        //cout << getBytes(ch, 8) << " ";
        // cout << (int) (ch >= 0 ? ch : (256 + ch)) << "\t";
    }
    cout << endl;
    f01.close();
    f02.close();

    ifstream f1(file1), f2(file2);
    while (true) {
        string str1, str2;
        if (getline(f1, str1)) {
            if (!getline(f2, str2)) {
                f1.close();
                f2.close();
                return false;
            }
        } else {
            if (getline(f2, str2)) {
                f1.close();
                f2.close();
                return false;
            } else {
                break;
            }
        }
        if (str1 != str2) {
            f1.close();
            f2.close();
            return false;
        }
    }
    f1.close();
    f2.close();
    return true;
}


 int main () {

     CLinker().addFile("0010_0.o").addFile("0010_1.o").addFile("0010_2.o").addFile("0010_3.o").linkOutput("0010_out",
                                                                                                          "pdrolowjjgdwxiadj");

     std::cout << "0010_out: " << std::boolalpha << identicalFiles("0010_out", "0010_ref") << std::endl;
     CLinker().addFile("0011_0.o").addFile("0011_1.o").linkOutput("0011_out", "yntvlhvtp");
        std::cout << "0011_out: " << std::boolalpha << identicalFiles("0011_out", "0011_ref") << std::endl;
     CLinker().addFile("0012_0.o").addFile("0012_1.o").addFile("0012_2.o").linkOutput("0012_out", "acnskqfuegem");
        std::cout << "0012_out: " << std::boolalpha << identicalFiles("0012_out", "0012_ref") << std::endl;
     CLinker().addFile("0013_0.o").addFile("0013_1.o").addFile("0013_2.o").linkOutput("0013_out", "yvjbkannhcusuktuhl");
        std::cout << "0013_out: " << std::boolalpha << identicalFiles("0013_out", "0013_ref") << std::endl;

     CLinker().addFile("0014_0.o").addFile("0014_1.o").addFile("0014_2.o").linkOutput("0014_out", "adqcwiahautvfi");
        std::cout << "0014_out: " << std::boolalpha << identicalFiles("0014_out", "0014_ref") << std::endl;


     return 0;
 }