/****************************************************************************
 * Match WUP folders to Title IDs listed in fs:/vol/external01/install/ID.txt
 ***************************************************************************/
#include "IdListSelect.hpp"
#include "CFile.hpp"
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static const char * const kIdTxtPath = "fs:/vol/external01/install/ID.txt";

static bool IsHexChar(char c)
{
	return (c >= '0' && c <= '9')
		|| (c >= 'a' && c <= 'f')
		|| (c >= 'A' && c <= 'F');
}

static std::string NormalizeTitleIdLine(const char *line)
{
	if(!line)
		return "";

	std::string hex;
	for(const char *p = line; *p; ++p)
	{
		char c = *p;
		if(c == '#' || c == ';')
			break;
		if(IsHexChar(c))
			hex += (char)std::toupper((unsigned char)c);
	}

	if(hex.size() == 16)
		return hex;

	/* Allow 0x prefix or stray separators already stripped */
	if(hex.size() > 16)
		hex = hex.substr(0, 16);

	return hex.size() == 16 ? hex : "";
}

static bool ReadTitleIdFromTmd(const std::string &folderPath, std::string &outHex)
{
	static const int offsets[] = { 0x18C, 0x18 };
	const char *tmdPaths[] = {
		"/title.tmd",
		"/meta/title.tmd"
	};

	for(size_t t = 0; t < sizeof(tmdPaths) / sizeof(tmdPaths[0]); ++t)
	{
		std::string path = folderPath + tmdPaths[t];
		CFile file(path.c_str(), CFile::ReadOnly);
		if(!file.isOpen())
			continue;

		for(size_t o = 0; o < sizeof(offsets) / sizeof(offsets[0]); ++o)
		{
			u8 buf[8];
			if(!file.seek(offsets[o], SEEK_SET))
				continue;
			if(file.read(buf, 8) != 8)
				continue;

			char hex[17];
			for(int i = 0; i < 8; ++i)
				std::snprintf(hex + i * 2, 3, "%02X", buf[i]);
			hex[16] = '\0';

			if(std::strncmp(hex, "0005", 4) != 0)
				continue;

			outHex = hex;
			return true;
		}
	}

	return false;
}

static bool GetFolderTitleId(CFolderList *list, int index, std::string &outHex)
{
	outHex.clear();
	const std::string name = list->GetName(index);
	const std::string path = list->GetPath(index);

	/* Try [xxxxxxxxxxxxxxxx] in display name first */
	const char *open = std::strchr(name.c_str(), '[');
	if(open)
	{
		++open;
		char hex[17];
		int n = 0;
		for(; n < 16 && IsHexChar(open[n]); ++n)
			hex[n] = (char)std::toupper((unsigned char)open[n]);
		hex[n] = '\0';
		if(n == 16 && std::strncmp(hex, "0005", 4) == 0)
		{
			outHex = hex;
			return true;
		}
	}

	return ReadTitleIdFromTmd(path, outHex);
}

static bool LoadIdTxtLines(std::vector<std::string> &outIds)
{
	outIds.clear();

	CFile file(kIdTxtPath, CFile::ReadOnly);
	if(!file.isOpen())
		return false;

	std::string content;
	u8 chunk[256];
	int read;
	while((read = file.read(chunk, sizeof(chunk) - 1)) > 0)
	{
		content.append(reinterpret_cast<const char *>(chunk), (size_t)read);
	}

	size_t start = 0;
	while(start <= content.size())
	{
		size_t end = content.find('\n', start);
		if(end == std::string::npos)
			end = content.size();

		std::string line = content.substr(start, end - start);
		while(!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
			line.pop_back();

		std::string id = NormalizeTitleIdLine(line.c_str());
		if(!id.empty())
			outIds.push_back(id);

		if(end >= content.size())
			break;
		start = end + 1;
	}

	return true;
}

int CFolderList_SelectFromIdTxt(CFolderList *list)
{
	if(!list || list->GetCount() <= 0)
		return 0;

	std::vector<std::string> wantIds;
	if(!LoadIdTxtLines(wantIds))
		return -1;

	if(wantIds.empty())
		return 0;

	list->UnSelectAll();

	int matched = 0;
	const int count = list->GetCount();

	for(size_t w = 0; w < wantIds.size(); ++w)
	{
		const std::string &want = wantIds[w];
		for(int i = 0; i < count; ++i)
		{
			std::string folderId;
			if(!GetFolderTitleId(list, i, folderId))
				continue;
			if(folderId != want)
				continue;
			if(!list->IsSelected(i))
			{
				list->Select(i);
				++matched;
			}
			break;
		}
	}

	return matched;
}
