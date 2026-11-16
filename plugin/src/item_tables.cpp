#include "item_tables.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <tuple>

namespace LootDrop {

namespace {

auto ReadFile(const std::filesystem::path& path) -> std::string {
	std::ifstream in(path, std::ios::binary);
	if (!in) {
		return {};
	}
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

auto SplitTabs(const std::string& line) -> std::vector<std::string> {
	std::vector<std::string> out;
	size_t start = 0;
	while (true) {
		const size_t tab = line.find('\t', start);
		out.push_back(line.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
		if (tab == std::string::npos) {
			break;
		}
		start = tab + 1;
	}
	if (!out.empty() && !out.back().empty() && out.back().back() == '\r') {
		out.back().pop_back();
	}
	return out;
}

struct Table {
	std::vector<std::string>              header;
	std::vector<std::vector<std::string>> rows;

	auto Column(const char* name) const -> int {
		for (size_t i = 0; i < header.size(); ++i) {
			if (header[i] == name) {
				return static_cast<int>(i);
			}
		}
		return -1;
	}
	static auto Cell(const std::vector<std::string>& row, int col) -> std::string {
		return col >= 0 && static_cast<size_t>(col) < row.size() ? row[static_cast<size_t>(col)] : std::string {};
	}
};

auto ReadTable(const std::filesystem::path& path) -> Table {
	Table t;
	std::istringstream in(ReadFile(path));
	std::string line;
	bool first = true;
	while (std::getline(in, line)) {
		if (first) {
			t.header = SplitTabs(line);
			first    = false;
		} else if (!line.empty() && line != "\r") {
			t.rows.push_back(SplitTabs(line));
		}
	}
	return t;
}

// Pulls "Key": "...", "enUS": "..." pairs out of a D2R string JSON file.
void ReadStrings(const std::filesystem::path& path, std::unordered_map<std::string, std::string>& out) {
	const std::string text = ReadFile(path);
	auto readValue = [&](size_t at) -> std::string {
		const size_t q1 = text.find('"', text.find(':', at) + 1);
		if (q1 == std::string::npos) return {};
		std::string value;
		for (size_t i = q1 + 1; i < text.size(); ++i) {
			const char c = text[i];
			if (c == '\\' && i + 1 < text.size()) {
				const char n = text[++i];
				value += n == 'n' ? '\n' : n;
			} else if (c == '"') {
				break;
			} else {
				value += c;
			}
		}
		return value;
	};
	size_t pos = 0;
	while ((pos = text.find("\"Key\"", pos)) != std::string::npos) {
		const std::string key = readValue(pos);
		const size_t      en  = text.find("\"enUS\"", pos);
		const size_t      next = text.find("\"Key\"", pos + 5);
		if (en != std::string::npos && (next == std::string::npos || en < next)) {
			std::string value = readValue(en);
			// Strip D2R colour codes such as "ÿc4".
			for (size_t c = value.find("\xC3\xBF" "c"); c != std::string::npos; c = value.find("\xC3\xBF" "c")) {
				value.erase(c, 4);
			}
			out.emplace(key, value);
		}
		pos += 5;
	}
}

auto FindExcelDir(const std::filesystem::path& modDirectory) -> std::filesystem::path {
	std::error_code ec;
	for (const auto& entry : std::filesystem::directory_iterator(modDirectory, ec)) {
		if (entry.is_directory(ec) && entry.path().extension() == L".mpq") {
			const auto excel = entry.path() / L"data" / L"global" / L"excel";
			if (std::filesystem::exists(excel / L"weapons.txt", ec)) {
				return excel;
			}
		}
	}
	const auto direct = modDirectory / L"data" / L"global" / L"excel";
	return std::filesystem::exists(direct / L"weapons.txt", ec) ? direct : std::filesystem::path {};
}

}

auto TrimCode(const std::string& code) -> std::string {
	std::string out = code;
	while (!out.empty() && (out.back() == ' ' || out.back() == '\0')) {
		out.pop_back();
	}
	return out;
}

auto RuneNumber(const std::string& code) -> int32_t {
	if (code.size() == 3 && code[0] == 'r' && std::isdigit(static_cast<unsigned char>(code[1])) && std::isdigit(static_cast<unsigned char>(code[2]))) {
		const int n = (code[1] - '0') * 10 + (code[2] - '0');
		return n >= 1 && n <= 33 ? n : 0;
	}
	return 0;
}

auto ItemTables::Load(const std::filesystem::path& modDirectory) -> std::string {
	const auto excel = FindExcelDir(modDirectory);
	if (excel.empty()) {
		return "item tables not found under the mod folder; names will show as codes";
	}
	const auto strings = excel.parent_path().parent_path() / L"local" / L"lng" / L"strings";
	for (const wchar_t* f : { L"item-names.json", L"item-runes.json", L"item-nameaffixes.json" }) {
		ReadStrings(strings / f, strings_);
	}

	for (const wchar_t* f : { L"weapons.txt", L"armor.txt", L"misc.txt" }) {
		const Table t       = ReadTable(excel / f);
		const int   nameCol = t.Column("name");
		const int   codeCol = t.Column("code");
		const int   strCol  = t.Column("namestr");
		const int   typeCol = t.Column("type");
		const int   type2Col = t.Column("type2");
		for (const auto& row : t.rows) {
			BaseItem item;
			item.code    = TrimCode(Table::Cell(row, codeCol));
			item.nameKey = Table::Cell(row, strCol);
			item.type    = Table::Cell(row, typeCol);
			item.type2   = Table::Cell(row, type2Col);
			item.name    = Lookup(item.nameKey);
			if (item.name.empty() || item.name == item.nameKey) {
				item.name = Table::Cell(row, nameCol);
			}
			const bool separator = item.code.empty();
			withSeparators_.push_back(item);
			if (!separator) {
				withoutSeparators_.push_back(item);
			}
		}
	}

	auto loadRecords = [&](const wchar_t* file, std::unordered_map<uint32_t, std::string>& out) {
		const Table t     = ReadTable(excel / file);
		const int   idx   = t.Column("index");
		const int   idCol = t.Column("*ID");
		uint32_t    row   = 0;
		for (const auto& r : t.rows) {
			const std::string key = Table::Cell(r, idx);
			const std::string id  = Table::Cell(r, idCol);
			if (!id.empty()) {
				out[static_cast<uint32_t>(std::strtoul(id.c_str(), nullptr, 10))] = key;
			} else if (!key.empty() && key != "Expansion") {
				out[row] = key;
			}
			++row;
		}
	};
	loadRecords(L"uniqueitems.txt", uniques_);
	loadRecords(L"setitems.txt", sets_);

	// Unique charms: uniques whose base item is a charm type.
	{
		const Table t     = ReadTable(excel / L"uniqueitems.txt");
		const int   idx   = t.Column("index");
		const int   idCol = t.Column("*ID");
		const int   code  = t.Column("code");
		for (const auto& r : t.rows) {
			const std::string id = Table::Cell(r, idCol);
			const BaseItem*   b  = BaseByCode(TrimCode(Table::Cell(r, code)));
			if (id.empty() || b == nullptr) continue;
			if (b->type == "scha" || b->type == "mcha" || b->type == "lcha" || b->type == "csch") {
				uniqueCharms_.push_back({ static_cast<uint32_t>(std::strtoul(id.c_str(), nullptr, 10)), Lookup(Table::Cell(r, idx)) });
			}
		}
	}

	// Grail totals: what Grail On! lists as uniques and set items, minus what cannot drop
	// (crafted and quest items such as Hellfire Torch, key and gem grabbers).
	for (const auto& [file, prefix, baseColumn] : { std::tuple { L"uniqueitems.txt", "u:", "code" }, std::tuple { L"setitems.txt", "s:", "item" } }) {
		const Table t     = ReadTable(excel / file);
		const int   idx   = t.Column("index");
		const int   dis   = t.Column("disabled");
		const int   spawn = t.Column("spawnable");
		const int   base  = t.Column(baseColumn);
		const int   idCol = t.Column("*ID");
		for (const auto& r : t.rows) {
			const std::string baseCode = TrimCode(Table::Cell(r, base));
			if (Table::Cell(r, dis) == "1" || baseCode.empty()) continue;
			if (Table::Cell(r, spawn) == "1") {
				++grailTotal_;
				const std::string id = Table::Cell(r, idCol);
				if (!id.empty()) {
					const BaseItem* b = BaseByCode(baseCode);
					catalog_.push_back({ prefix[0], static_cast<uint32_t>(std::strtoul(id.c_str(), nullptr, 10)), Lookup(Table::Cell(r, idx)), b ? b->name : baseCode });
				}
			} else {
				grailExcluded_.insert(prefix + Lookup(Table::Cell(r, idx)));
			}
		}
	}

	char summary[256];
	std::snprintf(summary, sizeof(summary), "item tables: %zu bases, %zu uniques, %zu set items, %zu strings, grail %u", withoutSeparators_.size(), uniques_.size(), sets_.size(), strings_.size(), grailTotal_);
	return summary;
}

auto ItemTables::Lookup(const std::string& key) const -> std::string {
	const auto it = strings_.find(key);
	return it != strings_.end() ? it->second : key;
}

auto ItemTables::Base(uint32_t classId) const -> const BaseItem* {
	const auto& v = choice_ == 0 ? withSeparators_ : withoutSeparators_;
	return classId < v.size() ? &v[classId] : nullptr;
}

auto ItemTables::BaseByCode(const std::string& code) const -> const BaseItem* {
	for (const auto& b : withoutSeparators_) {
		if (b.code == code) return &b;
	}
	return nullptr;
}

auto ItemTables::Calibrate(uint32_t classId, const std::string& code) -> bool {
	const std::string c = TrimCode(code);
	const std::vector<BaseItem>* lists[2] = { &withSeparators_, &withoutSeparators_ };
	for (int i = 0; i < 2; ++i) {
		const auto& v = *lists[i];
		if (classId < v.size() && v[classId].code == c) {
			++votes_[i];
		} else {
			++misses_[i];
		}
	}
	// Confirm once one ordering has three matches and no misses.
	for (int i = 0; i < 2; ++i) {
		if (votes_[i] >= 3 && misses_[i] == 0) {
			choice_     = i;
			calibrated_ = true;
		}
	}
	return calibrated_;
}

auto ItemTables::CalibrationSummary() const -> std::string {
	char buf[160];
	std::snprintf(buf, sizeof(buf), "class-id map: %s (with separators %u/%u, without %u/%u)", calibrated_ ? (choice_ == 0 ? "with separators" : "without separators") : "unconfirmed",
	              votes_[0], votes_[0] + misses_[0], votes_[1], votes_[1] + misses_[1]);
	return buf;
}

auto ItemTables::UniqueName(uint32_t recordId) const -> std::string {
	const auto it = uniques_.find(recordId);
	return it != uniques_.end() ? Lookup(it->second) : std::string {};
}

auto ItemTables::SetName(uint32_t recordId) const -> std::string {
	const auto it = sets_.find(recordId);
	return it != sets_.end() ? Lookup(it->second) : std::string {};
}

}
