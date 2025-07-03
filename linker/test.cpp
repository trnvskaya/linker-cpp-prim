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

/**
 * Class for linking object files into a single executable
 */
class CLinker {
public:
    CLinker() = default;

    ~CLinker() = default;

    CLinker(const CLinker &) = delete;

    CLinker &operator=(const CLinker &) = delete;

    CLinker &addFile(const std::string &fileName);

    void linkOutput(const std::string &fileName, const std::string &entryPoint);

private:
    /**
     * Structure for exported functions
     */
    struct ExportFun {
        std::string name; ///< Name of the exported function
        ui32 offset; ///< Offset in the file where the function is located
    };

    /**
     * Structure for imported functions
     */
    struct ImportFun {
        std::string name; ///< Name of the imported function
        std::vector<ui32> references; ///< References to functions in the file (place of use)
    };

    /**
     * Structure for the file header
     */
    struct Header {
        ui32 expCnt; ///< Number of exported functions
        ui32 imCnt; ///< Number of imported functions
        ui32 codeSz; ///< Size of the code section
    };

    /**
     * Structure for function information
     */
    struct FunInfo {
        size_t fileIdx; ///< Index of the file where the function is used
        ui32 offset; ///< Offset in the file
        ui32 size; ///< Size of the function in bytes
        ui32 offsetData; ///< Code offset
    };

    /**
     * Structure for the object file
     */
    struct ObjFile {
        std::vector<ImportFun> imports; ///< List of imported functions
        std::vector<ExportFun> exports; ///< List of exported functions
        Header fileHeader; ///< File header information
        ui32 offsetData; ///< Offset of the code in the file
    };

    void readFiles();

    void FunInfoMap();

    std::vector<ui8> loadCode(const FunInfo &functionIn);

    void bfs(const std::string &entryPoint, std::vector<std::string> &neededFunctions);

    std::vector<std::string> m_Files; ///< List of file names
    std::vector<ObjFile> m_ObjFiles; ///< List of object files
    std::unordered_map<std::string, std::shared_ptr<FunInfo>> m_FunctionInfoMap; ///< Map for information about functions
};

/**
 * Adds a file name to the list of files to be linked
 * @param fileName Name of the file to add
 * @return Reference to this CLinker instance
 */
CLinker &CLinker::addFile(const std::string &fileName) {
    m_Files.push_back(fileName);
    return *this;
}

/**
 * Reads and parses all object files
 * @throws std::runtime_error if file reading fails or duplicate symbols are found
 */
void CLinker::readFiles() {
    m_ObjFiles.clear();
    m_ObjFiles.reserve(m_Files.size());
    m_FunctionInfoMap.clear();

    std::map<std::string, size_t> exportDup;

    for (size_t k = 0; k < m_Files.size(); ++k) {
        const auto &fileName = m_Files[k];
        std::ifstream file(fileName, std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Failed reading input file");
        }
        ObjFile objFile;

        if (!file.read(reinterpret_cast<char *>(&objFile.fileHeader.expCnt), sizeof(objFile.fileHeader.expCnt)) ||
            !file.read(reinterpret_cast<char *>(&objFile.fileHeader.imCnt), sizeof(objFile.fileHeader.imCnt)) ||
            !file.read(reinterpret_cast<char *>(&objFile.fileHeader.codeSz), sizeof(objFile.fileHeader.codeSz))) {
            file.close();
            throw std::runtime_error("Failed reading input file");
        }

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

        objFile.offsetData = file.tellg();
        file.close();
        m_ObjFiles.push_back(std::move(objFile));
    }
}

/**
 * Creates a map of function information from the object files
 */
void CLinker::FunInfoMap() {
    m_FunctionInfoMap.clear();
    for (size_t i = 0; i < m_ObjFiles.size(); ++i) {
        const auto &objFile = m_ObjFiles[i];

        for (const auto &exportF: objFile.exports) {
            ui32 nextOffset = objFile.fileHeader.codeSz;

            for (const auto &otherExportF: objFile.exports) {
                if (otherExportF.offset > exportF.offset && otherExportF.offset < nextOffset) {
                    nextOffset = otherExportF.offset;
                }
            }

            ui32 funSize = nextOffset - exportF.offset;
            m_FunctionInfoMap[exportF.name] = std::make_shared<FunInfo>(
                    FunInfo{i, exportF.offset, funSize, objFile.offsetData});
        }
    }
}

/**
 * Loads the code of a function from the object file
 * @param functionIn Information about the function to load
 * @return Vector containing the function's code
 * @throws std::runtime_error if file reading fails
 */
std::vector<ui8> CLinker::loadCode(const FunInfo &functionIn) {
    std::ifstream file(m_Files[functionIn.fileIdx], std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed reading input file");
    }
    file.seekg(functionIn.offset + functionIn.offsetData, std::ios::beg);

    std::vector<ui8> code(functionIn.size);
    if (!file.read(reinterpret_cast<char *>(code.data()), functionIn.size)) {
        file.close();
        throw std::runtime_error("Failed reading input file");
    }
    file.close();
    return code;
}

/**
 * Performs breadth-first search to find all functions that are needed
 * @param entryPoint Starting function for the search
 * @param neededFunctions Vector to store the names of needed functions
 * @throws std::runtime_error if entry point or any required function is undefined
 */
void CLinker::bfs(const std::string &entryPoint, std::vector<std::string> &neededFunctions) {

    // entrypoint does not exist
    if (m_FunctionInfoMap.find(entryPoint) == m_FunctionInfoMap.end()) {
        throw std::runtime_error("Undefined symbol " + entryPoint);
    }

    std::set<std::string> visited = {entryPoint};
    std::deque<std::string> q = {entryPoint};

    while (!q.empty()) {
        std::string currFun = std::move(q.front());
        q.pop_front();

        neededFunctions.push_back(currFun);

        const auto &funInfo = m_FunctionInfoMap[currFun];

        for (const auto &importedFunction: m_ObjFiles[funInfo->fileIdx].imports) {

            bool used = false;

            for (const auto &ref: importedFunction.references) {
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
void CLinker::linkOutput(const std::string &fileName, const std::string &entryPoint) {
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
    for (const auto &fun: neededFunctions) {
        functionOffsets[fun] = currentOffset;
        currentOffset += m_FunctionInfoMap[fun]->size;
    }

    std::ofstream outFile(fileName, std::ios::binary);
    if (!outFile.is_open()) {
        throw std::runtime_error("Failed writing output file");
    }

    // create output file
    for (const auto &fun: neededFunctions) {
        const auto &funInfo = m_FunctionInfoMap[fun];
        size_t fileIdx = funInfo->fileIdx;

        // read the code only if needed
        std::vector<ui8> functionBody = loadCode(*funInfo);


        for (const auto &importFun: m_ObjFiles[fileIdx].imports) {
            // if the function is imported and is used, we add it to output
            // and change all calls of this function with the address of the function
            if (functionOffsets.find(importFun.name) != functionOffsets.end()) {
                ui32 targetAddress = functionOffsets[importFun.name];

                for (const auto &ref: importFun.references) {
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


#ifndef __PROGTEST__

int main() {
    CLinker().addFile("0in0.o").linkOutput("0out", "strlen");

    CLinker().addFile("1in0.o").linkOutput("1out", "main");

    CLinker().addFile("2in0.o").addFile("2in1.o").linkOutput("2out", "main");

    CLinker().addFile("3in0.o").addFile("3in1.o").linkOutput("3out", "towersOfHanoi");

    try {
        CLinker().addFile("4in0.o").addFile("4in1.o").linkOutput("4out", "unusedFunc");
        assert ("missing an exception" == nullptr);
    }
    catch (const std::runtime_error &e) {
        // e . what (): Undefined symbol qsort
    }
    catch (...) {
        assert ("invalid exception" == nullptr);
    }

    try {
        CLinker().addFile("5in0.o").linkOutput("5out", "main");
        assert ("missing an exception" == nullptr);
    }
    catch (const std::runtime_error &e) {
        // e . what (): Duplicate symbol: printf
    }
    catch (...) {
        assert ("invalid exception" == nullptr);
    }

    try {
        CLinker().addFile("6in0.o").linkOutput("6out", "strlen");
        assert ("missing an exception" == nullptr);
    }
    catch (const std::runtime_error &e) {
        // e . what (): Cannot read input file
    }
    catch (...) {
        assert ("invalid exception" == nullptr);
    }

    try {
        CLinker().addFile("7in0.o").linkOutput("7out", "strlen2");
        assert ("missing an exception" == nullptr);
    }
    catch (const std::runtime_error &e) {
        // e . what (): Undefined symbol strlen2
    }
    catch (...) {
        assert ("invalid exception" == nullptr);
    }

    return EXIT_SUCCESS;
}

#endif /* __PROGTEST__ */
