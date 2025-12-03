#include "Job.h"
#include "Utils.h"

Job::Job(const std::string& blobHexIn, const std::string& jobIdIn,
         const std::string& targetIn, uint64_t heightIn, const std::string& seedHashIn)
    : jobId(jobIdIn), blobHex(blobHexIn), target(targetIn), 
      seedHash(seedHashIn), height(heightIn), difficulty(0) {
    blob = Utils::hexToBytes(blobHexIn);
}

Job::Job(const Job& other)
    : jobId(other.jobId), blobHex(other.blobHex), blob(other.blob),
      target(other.target), seedHash(other.seedHash), 
      height(other.height), difficulty(other.difficulty) {
}

Job& Job::operator=(const Job& other) {
    if (this != &other) {
        jobId = other.jobId;
        blobHex = other.blobHex;
        blob = other.blob;
        target = other.target;
        seedHash = other.seedHash;
        height = other.height;
        difficulty = other.difficulty;
    }
    return *this;
}

bool Job::isValid() const {
    return !jobId.empty() && !blob.empty() && blob.size() >= 43;
}

void Job::clear() {
    jobId.clear();
    blobHex.clear();
    blob.clear();
    target.clear();
    seedHash.clear();
    height = 0;
    difficulty = 0;
}