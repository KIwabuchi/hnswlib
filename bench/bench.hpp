#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

using PointT = float;
using DistanceT = float;
using PointStore = std::vector<std::pair<uint64_t, std::vector<PointT>>>;
using NeighborsTableT =
    std::vector<std::vector<std::pair<DistanceT, hnswlib::labeltype>>>;

inline std::chrono::steady_clock::time_point startTimer() {
  return std::chrono::steady_clock::now();
}

inline double geElapsedTime(const std::chrono::steady_clock::time_point &tic) {
  auto duration_time = std::chrono::steady_clock::now() - tic;
  return static_cast<double>(
             std::chrono::duration_cast<std::chrono::microseconds>(
                 duration_time)
                 .count()) /
         1e6;
}

/// \brief Search file paths recursively.
/// If a directory path is given, it returns all the file paths in the directory
/// and subdirectories. If a file path is given, it returns the file path.
inline std::vector<std::string> findFilePaths(const std::string_view path) {
  std::vector<std::string> paths;
  if (std::filesystem::is_regular_file(std::filesystem::path(path))) {
    paths.emplace_back(path);
  } else {
    for (const auto &entry :
         std::filesystem::recursive_directory_iterator(path)) {
      if (entry.is_regular_file()) paths.emplace_back(entry.path());
    }
  }
  return paths;
}

inline void readPoints(const std::vector<std::string> &file_paths,
                       const std::size_t dim, PointStore &points) {
  std::size_t num_points = 0;
// count the number of lines in the files in parallel
#pragma omp parallel for reduction(+ : num_points)
  for (std::size_t i = 0; i < file_paths.size(); i++) {
    const auto file_path = file_paths[i];
    std::ifstream ifs(file_path);
    if (!ifs) {
      std::cerr << "Failed to open " << file_path << std::endl;
      std::abort();
    }
    std::string buf;
    while (std::getline(ifs, buf)) {
      num_points++;
    }
  }
  std::cout << "Read " << num_points << " points" << std::endl;
  points.clear();
  points.resize(num_points);

  std::size_t offset = 0;
#pragma omp parallel for
  for (std::size_t i = 0; i < file_paths.size(); i++) {
    const auto file_path = file_paths[i];
    std::ifstream ifs(file_path);
    if (!ifs) {
      std::cerr << "Failed to open " << file_path << std::endl;
      std::abort();
    }

    std::string buf;
    while (std::getline(ifs, buf)) {
      uint64_t pos = 0;
#pragma omp critical
      { pos = offset++; }

      std::istringstream iss(buf);
      uint64_t id;
      iss >> id;
      points[pos].first = id;
      points[pos].second.resize(dim);
      for (std::size_t i = 0; i < dim; i++) {
        iss >> points[pos].second[i];
      }
    }
  }
  if (offset != num_points) {
    std::cerr << "offset != num_points" << std::endl;
    std::abort();
  }
}

template <typename T>
inline void readQueries(const std::string &file_path, const std::size_t dim,
                        std::vector<std::vector<T>> &queries) {
  std::cout << "Reading " << file_path << std::endl;
  std::ifstream ifs(file_path);
  if (!ifs) {
    std::cerr << "Failed to open " << file_path << std::endl;
    return;
  }

  std::string buf;
  uint32_t id = 0;
  while (std::getline(ifs, buf)) {
    std::istringstream iss(buf);
    queries.emplace_back();
    auto &query = queries[id];
    query.resize(dim);
    for (std::size_t i = 0; i < dim; i++) {
      iss >> query[i];
    }
    ++id;
  }
  std::cout << "Read " << id << " queries" << std::endl;
}

inline void readGroundTruth(const std::string file_path,
                            const std::size_t num_queries, const std::size_t k,
                            NeighborsTableT &gt) {
  std::ifstream ifs(file_path);
  if (!ifs) {
    std::cerr << "Failed to open " << file_path << std::endl;
    return;
  }

  std::string buf;
  gt.resize(num_queries);

  uint32_t id = 0;
  for (std::size_t n = 0; n < num_queries; n++) {
    std::getline(ifs, buf);
    std::istringstream iss(buf);
    gt[id].resize(k);
    for (std::size_t i = 0; i < k; i++) {
      iss >> gt[id][i].second;
    }
    ++id;
  }

  id = 0;
  for (std::size_t n = 0; n < num_queries; n++) {
    std::getline(ifs, buf);
    std::istringstream iss(buf);
    gt[id].resize(k);
    for (std::size_t i = 0; i < k; i++) {
      iss >> gt[id][i].first;
    }
    ++id;
  }
}

template <class OutrContainer>
inline typename OutrContainer::container_type &getContainer(OutrContainer &a) {
  struct tmp : OutrContainer {
    static typename OutrContainer::container_type &get(OutrContainer &a) {
      return a.*&tmp::c;
    }
  };
  return tmp::get(a);
}

template <typename T>
inline bool nearlyEqual(const T a, const T b,
                        const double eps = std::numeric_limits<T>::epsilon()) {
  return (std::fabs(a - b) < eps);
}

/// \brief Calculate exact recall@k scores.
/// Test result IDs must exist in ground truth.
/// Distance values are ignored.
inline std::vector<double> getExactRecallScores(
    const NeighborsTableT &test_result, const NeighborsTableT &ground_truth,
    const std::size_t k = 10) {
  if (ground_truth.size() != test_result.size()) {
    std::cerr << "#of ground truth and test result entries are different: "
              << test_result.size() << " != " << ground_truth.size()
              << std::endl;
    return {};
  }

  std::vector<double> scores;
  for (std::size_t i = 0; i < test_result.size(); ++i) {
    if (test_result[i].size() < k) {
      std::cerr << "#of elements in " << i << "-th test result ("
                << test_result[i].size() << ") < k (" << k << ")" << std::endl;
      return {};
    }
    if (ground_truth[i].size() < k) {
      std::cerr << "#of elements in " << i << "-th ground truth ("
                << ground_truth[i].size() << ") < k (" << k << ")" << std::endl;
      return {};
    }

    auto sorted_test = test_result[i];
    std::sort(sorted_test.begin(), sorted_test.end());

    auto sorted_gt = ground_truth[i];
    std::sort(sorted_gt.begin(), sorted_gt.end());

    std::unordered_set<hnswlib::labeltype> true_id_set;
    for (std::size_t n = 0; n < k; ++n) {
      true_id_set.insert(sorted_gt[n].second);
    }

    std::size_t num_corrects = 0;
    for (std::size_t n = 0; n < k; ++n) {
      num_corrects += true_id_set.count(sorted_test[n].second);
    }

    scores.push_back((double)num_corrects / (double)k * 100.0);
  }
  return scores;
}

/// \brief Calculate recall@k scores, accepting distance ties.
/// More than k ground truth neighbors are used in the recall calculation,
/// if their distances are tied with k-th ground truth neighbor.
/// \tparam T Element type.
/// \param test_result Test result set.
/// \param ground_truth Ground truth set.
/// \param k Calculates recall@k.
/// \param epsilon Tolerance for distance comparison.
/// \return Returns recall scores.
inline std::vector<double> getExactRecallScoresWithDistanceTies(
    const NeighborsTableT &test_result, const NeighborsTableT &ground_truth,
    const std::size_t k = 10, const double epsilon = 1e-6) {
  if (ground_truth.size() != test_result.size()) {
    std::cerr << "#of ground truth and test result entries are different: "
              << test_result.size() << " != " << ground_truth.size()
              << std::endl;
    return {};
  }

  std::vector<double> scores;
  for (std::size_t i = 0; i < test_result.size(); ++i) {
    if (test_result[i].size() < k) {
      std::cerr << "#of elements in " << i << "-th test result ("
                << test_result[i].size() << ") < k (" << k << ")" << std::endl;
      return {};
    }
    if (ground_truth[i].size() < k) {
      std::cerr << "#of elements in " << i << "-th ground truth ("
                << ground_truth[i].size() << ") < k (" << k << ")" << std::endl;
      return {};
    }

    auto sorted_test = test_result[i];
    std::sort(sorted_test.begin(), sorted_test.end());

    auto sorted_gt = ground_truth[i];
    std::sort(sorted_gt.begin(), sorted_gt.end());

    std::unordered_set<hnswlib::labeltype> true_id_set;
    const auto max_distance = sorted_gt[k - 1].first;
    for (std::size_t n = 0; n < sorted_gt.size(); ++n) {
      if (n >= k && !nearlyEqual(sorted_gt[n].first, max_distance, epsilon))
        break;
      true_id_set.insert(sorted_gt[n].second);
    }

    std::size_t num_corrects = 0;
    for (std::size_t n = 0; n < k; ++n) {
      num_corrects += true_id_set.count(sorted_test[n].second);
    }

    scores.push_back((double)num_corrects / (double)k * 100.0);
  }
  return scores;
}

std::vector<std::string> split(const std::string &s, const char del) {
  std::stringstream ss(s);
  std::string word;
  std::vector<std::string> list;
  while (!ss.eof()) {
    std::getline(ss, word, del);
    list.push_back(word);
  }
  return list;
}