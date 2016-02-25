#include "zenParser.h"
#include <fstream>
#include "parserImplASCII.h"
#include "parserImplBinSafe.h"
#include "parserImplBinary.h"
#include <algorithm>
#include <cctype>
#include "utils/logger.h"
#include "oCWorld.h"
#include "zCMesh.h"

using namespace ZenConvert;

/**
* @brief reads a zen from a file
*/
ZenParser::ZenParser(const std::string& file) :
	m_Seek(0),
	m_pWorldMesh(0)
{
	// Get data from zenfile
	readFile(file, m_Data);
}

/**
 * @brief reads a zen from memory
 */
ZenParser::ZenParser(const void* data, size_t size) :
	m_Seek(0),
	m_pWorldMesh(0)
{
	m_Data.resize(size);
	memcpy(m_Data.data(), data, size);
}

ZenConvert::ZenParser::~ZenParser()
{
	delete m_pWorldMesh;
}

/**
* @brief Read the given file and places the data in the given vector
*/
bool ZenParser::readFile(const std::string& fileName, std::vector<uint8_t>& data)
{
	std::ifstream file(fileName, std::ios::in | std::ios::ate | std::ios::binary);
	size_t size = file.tellg();
	file.seekg(0, std::ios::beg);

	if(!file.good())
		throw std::runtime_error("File does not exist");

	data.resize(size);
	file.read(reinterpret_cast<char *>(data.data()), size);

	return true;
}

/**
* @brief returns whether the given string is a number
*/
bool ZenParser::isNumber(const std::string &expr)
{
	return !expr.empty() && std::find_if(expr.begin(), expr.end(), [](char c){return !std::isdigit(c); }) == expr.end();
}

/**
* @brief Reads the main ZEN-Header
*/
void ZenParser::readHeader()
{
	if(!skipString("ZenGin Archive"))
		throw std::runtime_error("Not a valid format");

	if(!skipString("ver"))
		throw std::runtime_error("Not a valid header");

	// Version should be always 1...
	m_Header.version = readIntASCII();

	// Skip archiver type
	skipString();

	// Read file-type, to create the right archiver implementation
	std::string fileType = readLine();
	if(fileType == "ASCII")
	{
		m_Header.fileType = FT_ASCII;
		m_pParserImpl = new ParserImplASCII(this);
	}
	else if(fileType == "BINARY")
	{
		m_Header.fileType = FT_BINARY;
		m_pParserImpl = new ParserImplBinary(this);
	}
	else if(fileType == "BIN_SAFE")
	{
		m_Header.fileType = FT_BINSAFE;
		m_pParserImpl = new ParserImplBinSafe(this);
	}
	else
		throw std::runtime_error("Unsupported file format");

	// Read string of the format "savegame b", where b is 0 or 1
	if(!skipString("saveGame"))
		throw std::runtime_error("Unsupported file format");

	m_Header.saveGame = readBoolASCII();

	// Read possible date
	if(skipString("date"))
	{
		m_Header.date = readString() + " ";
		m_Header.date += readString();
	}

	// Skip possible user
	if(skipString("user"))
		m_Header.user = readString();

	// Reached the end of the main header
	if(!skipString("END"))
		throw std::runtime_error("No END in header(1)");

	// Continue with the implementationspecific header
	skipSpaces();

	m_pParserImpl->readImplHeader();
}

/**
* @brief reads the main oCWorld-Object, found in the level-zens
*/
void ZenParser::readWorld()
{
	readChunkTest();
}

void ZenParser::readWorldMesh()
{
	m_pWorldMesh = new zCMesh;

	// Read worldmesh, if needed
	if(m_pWorldMesh)
	{
		m_pWorldMesh->readObjectData(*this, true);
	}
	else
	{
		// Skip
		readBinaryDWord(); // Version
		m_Seek += readBinaryDWord();
	}
}

/**
* @brief reads a full chunk (TESTING ONLY)
*/
void ZenParser::readChunkTest()
{
	ChunkHeader header;
	m_pParserImpl->readChunkStart(header);

	if(header.name == "MeshAndBsp")
	{
		readWorldMesh();
		return;
	}

	if(header.classname != "oCWorld:zCWorld")
		return;

	std::string str;
	do
	{
		size_t ts = m_Seek;
		ParserImpl::EZenValueType type;
		size_t size;
		

		m_pParserImpl->readEntryType(type, size);

		if(type != ParserImpl::ZVT_STRING)
		{
			m_Seek = ts;
			std::vector<uint8_t> data(size);
			m_pParserImpl->readEntry("", data.data(), size, ParserImpl::ZVT_0);
		}

		if(type == ParserImpl::ZVT_STRING)
		{
			m_Seek = ts;
			std::string str; str.resize(size);
			m_pParserImpl->readEntry("", &str, size, ParserImpl::ZVT_STRING);

			if(str.front() == '[' && str.size() > 2)
			{
				m_Seek = ts;
				readChunkTest();
				return;
			}
		}
	}while(str != "[]");
}

/**
* @brief Reads a chunk-header
*/
void ZenParser::readChunkStart(ChunkHeader& header)
{
	m_pParserImpl->readChunkStart(header);
}

/**
* @brief Reads the end of a chunk
*/
void ZenParser::readChunkEnd()
{
	m_pParserImpl->readChunkEnd();
}

/**
 * @brief reads an ASCII datatype from the loaded file
 */
int32_t ZenParser::readIntASCII()
{
	skipSpaces();
	std::string number;
	while(m_Data[m_Seek] >= '0' && m_Data[m_Seek] <= '9')
	{
		number += m_Data[m_Seek];
		++m_Seek;
	}
	return std::stoi(number);
}

/**
 * @brief reads an ASCII datatype from the loaded file
 */
bool ZenParser::readBoolASCII()
{
	skipSpaces();
	bool retVal;
	if(m_Data[m_Seek] != '0' && m_Data[m_Seek] != '1')
		throw std::runtime_error("Value is not a bool");
	else
		retVal = m_Data[m_Seek] == '0' ? false : true;

	++m_Seek;
	return retVal;
}

/**
 * @brief Reads a string until \r, \n or a space is found
 */
std::string ZenParser::readString(bool skip)
{
	if(skip)
		skipSpaces();

	std::string str;
	while(m_Data[m_Seek] != '\r' && m_Data[m_Seek] != '\n' && m_Data[m_Seek] != ' ')
	{
		str += m_Data[m_Seek];
		++m_Seek;
	}
	return str;
}

/**
 * @brief skips a string and checks if it matches the given expected one
 */
bool ZenParser::skipString(const std::string &pattern)
{
	skipSpaces();
	bool retVal = true;
	if(pattern.empty())
	{
		while(!(m_Data[m_Seek] == '\n' || m_Data[m_Seek] == ' '))
			++m_Seek;
		++m_Seek;
	}
	else
	{
		size_t lineSeek = 0;
		while(lineSeek < pattern.size())
		{
			if(m_Data[m_Seek] != pattern[lineSeek])
			{
				retVal = false;
				break;
			}

			++m_Seek;
			++lineSeek;
		}
	}

	return retVal;
}

/**
* @brief Skips all whitespace-characters until it hits a non-whitespace one
*/
void ZenParser::skipSpaces()
{
	bool search = true;
	while(search)
	{
		checkArraySize();
		switch(m_Data[m_Seek])
		{
		case ' ':
		case '\r':
		case '\t':
		case '\n':
			++m_Seek;
			break;
		default:
			search = false;
			break;
		}
	}
}

/**
* @brief Throws an exception if the current seek is behind the file-size
*/
void ZenParser::checkArraySize()
{
	if(m_Seek >= m_Data.size())
		throw std::logic_error("Out of range");
}

/**
* @brief Reads the given type as binary data and returns it
*/
uint32_t ZenParser::readBinaryDWord()
{
	uint32_t retVal = *reinterpret_cast<uint32_t *>(&m_Data[m_Seek]);
	m_Seek += sizeof(uint32_t);
	return retVal;
}

uint16_t ZenParser::readBinaryWord()
{
	uint16_t retVal = *reinterpret_cast<uint16_t *>(&m_Data[m_Seek]);
	m_Seek += sizeof(uint16_t);
	return retVal;
}

uint8_t ZenParser::readBinaryByte()
{
	uint8_t retVal = *reinterpret_cast<uint8_t *>(&m_Data[m_Seek]);
	m_Seek += sizeof(uint8_t);
	return retVal;
}

float ZenParser::readBinaryFloat()
{
	float retVal = *reinterpret_cast<float *>(&m_Data[m_Seek]);
	m_Seek += sizeof(float);
	return retVal;
}

void ZenParser::readBinaryRaw(void* target, size_t numBytes)
{
	memcpy(target, &m_Data[m_Seek], numBytes);
	m_Seek += numBytes;
}


/**
* @brief Reads a line to \r or \n
*/
std::string ZenParser::readLine(bool skip)
{
	std::string retVal;
	while(m_Data[m_Seek] != '\r' && m_Data[m_Seek] != '\n' && m_Data[m_Seek] != '\0')
	{
		checkArraySize();
		retVal += m_Data[m_Seek++];
	}

	// Skip trailing \n\r\0
	m_Seek++;

	if(skip)
		skipSpaces();
	return retVal;
}