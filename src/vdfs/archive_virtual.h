#pragma once
#include <string>
#include <vector>

namespace VDFS
{
	// These files are written on 32-bit, no packing
#pragma pack(push, 1)

	// Some of these are gracefully taken from the GothicFix-Teams VDFS implementation.

	/**
	* @brief Timestamp-bitfield for vdfs-files
	*/
	struct VdfTime  
	{
		uint32_t Seconds : 5;
		uint32_t Minutes : 6;   
		uint32_t Hour	 : 5;  
		uint32_t Day	 : 5;   
		uint32_t Month	 : 4;   
		uint32_t Year	 : 7;
	};

	/**
	* @brief VDFS-File header
	*/
	struct VdfHeader
	{
		char Comment[256];
		char Signature[16];
		uint32_t NumEntries;
		uint32_t NumFiles;
		VdfTime Timestamp;
		uint32_t DataSize;
		uint32_t RootCatOffset;
		uint32_t Version; // 0x50
	};

	/**
	* @brief single file entry information
	*/
	struct VdfEntryInfo
	{
		char Name[64];
		uint32_t JumpTo; // Dirs = child entry's number, Files = data offset
		uint32_t Size;
		uint32_t Type; // = 0x00000000 for files or VDFS_ENTRY_DIR, may be bitmasked by VDFS_ENTRY_LAST
		uint32_t Attributes; // 20 = A;
	};
#pragma pack(pop)

	class ArchiveVirtual
	{
	public:
		ArchiveVirtual();
		~ArchiveVirtual();

		/**
		 * @brief Loads the given VDFS-File and initializes the index
		 */
		bool LoadVDF(const std::string& file);

		/**
		 * @brief Updates the file catalog of this archive
		 */
		bool UpdateFileCatalog();

	protected:
		/**
		 * @brief File-Stream for this archive
		 */
		FILE* m_pStream;

		/**
		 * @brief Game-Version this is from
		 */
		enum
		{
			AV_Gothic1,
			AV_Gothic2
		} m_ArchiveVersion;

		/**
		 * @brief Header of the loaded VDF-Archive
		 */
		VdfHeader m_VdfHeader;

		/**
		 * @brief File catalog of files of this archive
		 */
		std::vector<VdfEntryInfo> m_EntryCatalog;
	};

	// TODO: Make an archive-type for physical as well. Then use virtual functions to simplyfy access
	typedef ArchiveVirtual Archive;
}