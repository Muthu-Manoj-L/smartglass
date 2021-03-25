#include "decoder_utils.h"

#include <algorithm>
#include <cmath>
#include <limits>

//decoding is to find a word string W which maximizes equation:  Pnet(W;X) * Plm(W)^α * |W|^β


std::vector<std::pair<size_t, float>> get_pruned_log_probs(
    const double *prob_step,
    size_t class_dim,
    double cutoff_prob,
    size_t cutoff_top_n) {
  // cutoff one-step probs by cutoff_prob and cutoff_top_n
  // return ln probs bigger first
  std::vector<std::pair<int, double>> prob_idx; // <id, prob>
  for (size_t i = 0; i < class_dim; ++i) {
    prob_idx.push_back(std::pair<int, double>(i, prob_step[i]));
  }

  // pruning of vacobulary
  size_t cutoff_len = class_dim;
  if (cutoff_prob < 1.0 || cutoff_top_n < cutoff_len) {
    // sort by prob in decending order
    std::sort(
        prob_idx.begin(), prob_idx.end(), pair_comp_second_rev<int, double>);
    if (cutoff_prob < 1.0) {
      double cum_prob = 0.0;
      cutoff_len = 0;
      for (size_t i = 0; i < prob_idx.size(); ++i) {
        cum_prob += prob_idx[i].second;
        cutoff_len += 1;
        if (cum_prob >= cutoff_prob || cutoff_len >= cutoff_top_n) break;
      }
    }
    prob_idx = std::vector<std::pair<int, double>>(
        prob_idx.begin(), prob_idx.begin() + cutoff_len);
  }

  std::vector<std::pair<size_t, float>> log_prob_idx;
  for (size_t i = 0; i < cutoff_len; ++i) {
    log_prob_idx.push_back(std::pair<int, float>(
        prob_idx[i].first, log(prob_idx[i].second + NUM_FLT_MIN)));
  }
  return log_prob_idx;
}


size_t get_utf8_str_len(const std::string &str) {
  // utf8 string length
  size_t str_len = 0;
  for (char c : str) {
    str_len += ((c & 0xc0) != 0x80);
  }
  return str_len;
}

std::vector<std::string> split_into_codepoints(const std::string &str) {
  // utf8 string split
  // https://stackoverflow.com/questions/3911536/utf-8-unicode-whats-with-0xc0-and-0x80
  std::vector<std::string> result;
  std::string out_str;

  for (char c : str) {
    if (byte_is_codepoint_boundary(c)) {
      if (!out_str.empty()) {
        result.push_back(out_str);
        out_str.clear();
      }
    }

    out_str.append(1, c);
  }
  result.push_back(out_str);
  return result;
}

std::vector<std::string> split_into_bytes(const std::string &str) {
  // utf8 split into bytes
  std::vector<std::string> result;

  for (char c : str) {
    std::string ch(1, c);
    result.push_back(ch);
  }

  return result;
}


std::vector<std::string> split_str(const std::string &s,
                                   const std::string &delim) {
  //split string by delimeter
  std::vector<std::string> result;
  std::size_t start = 0, delim_len = delim.size();
  while (true) {
    std::size_t end = s.find(delim, start);
    if (end == std::string::npos) {
      if (start < s.size()) {
        result.push_back(s.substr(start));
      }
      break;
    }

    if (end > start) {
      result.push_back(s.substr(start, end - start));
    }
    start = end + delim_len;
  }
  return result;
}


// std::partial_sort comp function
//The value returned indicates whether the element passed as first argument is considered to go before the second
// sort by prefix's score
bool prefix_compare(const PathTrie *x, const PathTrie *y) {
  if (x->score == y->score) {
    if (x->character == y->character) {
      return false;
    } else {
      return (x->character < y->character);
    }
  } else {
    return x->score > y->score;
  }
}

// std::partial_sort comp function
//The value returned indicates whether the element passed as first argument is considered to go before the second
// sort by golbal scores
bool prefix_compare_external(const PathTrie *x, const PathTrie *y, const std::unordered_map<const PathTrie*, float>& scores) {
  if (scores.at(x) == scores.at(y)) {
    if (x->character == y->character) {
      return false;
    } else {
      return (x->character < y->character);
    }
  } else {
    return scores.at(x) > scores.at(y);
  }
}

// Lexicon FST
void add_word_to_fst(const std::vector<unsigned int> &word,
                     fst::StdVectorFst *dictionary) {
  if (dictionary->NumStates() == 0) {
    fst::StdVectorFst::StateId start = dictionary->AddState();
    assert(start == 0);
    dictionary->SetStart(start);
  }

  fst::StdVectorFst::StateId src = dictionary->Start();
  fst::StdVectorFst::StateId dst;
  for (auto c : word) {
    dst = dictionary->AddState();
    dictionary->AddArc(src, fst::StdArc(c, c, 0, dst));
    src = dst;
  }
  dictionary->SetFinal(dst, fst::StdArc::Weight::One());
}


// words based is utf8 False, chararactor based is utf8 True
// DeepSpeech/native_client/generate_scorer_package.cpp
// utf8 is False
// hello -> h e l l o <space>
// 我 -> 我 <space>
// utf8 is True
// 我 -> 0xAA 0xBB 0xCC
// convert charecter to bytes, 0xbe, 0x3f...
bool add_word_to_dictionary(
    const std::string &word,
    const std::unordered_map<std::string, int> &char_map,
    bool utf8,
    int SPACE_ID,
    fst::StdVectorFst *dictionary) {
  // Lexicon: word -> chars
  auto characters = utf8 ? split_into_bytes(word) : split_into_codepoints(word);

  std::vector<unsigned int> int_word;
  for (auto &c : characters) {
    auto int_c = char_map.find(c);
    if (int_c != char_map.end()) {
      int_word.push_back(int_c->second);
    } else {
      return false;  // return without adding
    }
  }

  if (!utf8) {
    int_word.push_back(SPACE_ID);
  }

  add_word_to_fst(int_word, dictionary);
  return true;  // return with successful adding
}