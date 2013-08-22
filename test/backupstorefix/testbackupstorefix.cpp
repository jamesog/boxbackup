// --------------------------------------------------------------------------
//
// File
//		Name:    testbackupstorefix.cpp
//		Purpose: Test BackupStoreCheck functionality
//		Created: 23/4/04
//
// --------------------------------------------------------------------------


#include "Box.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <string>
#include <map>

#include "Test.h"
#include "BackupClientCryptoKeys.h"
#include "BackupStoreCheck.h"
#include "BackupStoreConstants.h"
#include "BackupStoreContext.h"
#include "BackupStoreDirectory.h"
#include "BackupStoreException.h"
#include "BackupStoreFile.h"
#include "BackupStoreFileWire.h"
#include "BackupStoreFileEncodeStream.h"
#include "BackupStoreInfo.h"
#include "BufferedWriteStream.h"
#include "FileStream.h"
#include "RaidFileController.h"
#include "RaidFileException.h"
#include "RaidFileRead.h"
#include "RaidFileWrite.h"
#include "ServerControl.h"
#include "StoreStructure.h"
#include "ZeroStream.h"

#include "MemLeakFindOn.h"

/* 

Errors checked:

make some BackupDirectoryStore objects, CheckAndFix(), then verify
	- multiple objects with same ID
	- wrong order of old flags
	- all old flags

delete store info
add spurious file
delete directory (should appear again)
change container ID of directory
delete a file
double reference to a file inside a single dir
modify the object ID of a directory
delete directory, which has no members (will be removed)
extra reference to a file in another dir (higher ID to allow consistency -- use something in subti)
delete dir + dir2 in dir/dir2/file where nothing in dir2 except file, file should end up in lost+found
similarly with a dir, but that should get a dirxxx name
corrupt dir
corrupt file
delete root, copy a file to it instead (equivalent to deleting it too)

*/

std::string storeRoot("backup/01234567/");
int discSetNum = 0;

std::map<std::string, int32_t> nameToID;
std::map<int32_t, bool> objectIsDir;

#define RUN_CHECK	\
	::system(BBSTOREACCOUNTS " -c testfiles/bbstored.conf check 01234567"); \
	::system(BBSTOREACCOUNTS " -c testfiles/bbstored.conf check 01234567 fix");

// Get ID of an object given a filename
int32_t getID(const char *name)
{
	std::map<std::string, int32_t>::iterator i(nameToID.find(std::string(name)));
	TEST_THAT(i != nameToID.end());
	if(i == nameToID.end()) return -1;
	
	return i->second;
}

// Get the RAID filename of an object
std::string getObjectName(int32_t id)
{
	std::string fn;
	StoreStructure::MakeObjectFilename(id, storeRoot, discSetNum, fn, false);
	return fn;
}

// Delete an object
void DeleteObject(const char *name)
{
	RaidFileWrite del(discSetNum, getObjectName(getID(name)));
	del.Delete();
}

// Load a directory
void LoadDirectory(const char *name, BackupStoreDirectory &rDir)
{
	std::auto_ptr<RaidFileRead> file(RaidFileRead::Open(discSetNum, getObjectName(getID(name))));
	rDir.ReadFromStream(*file, IOStream::TimeOutInfinite);
}
// Save a directory back again
void SaveDirectory(const char *name, const BackupStoreDirectory &rDir)
{
	RaidFileWrite d(discSetNum, getObjectName(getID(name)));
	d.Open(true /* allow overwrite */);
	rDir.WriteToStream(d);
	d.Commit(true /* write now! */);
}

void CorruptObject(const char *name, int start, const char *rubbish)
{
	int rubbish_len = ::strlen(rubbish);
	std::string fn(getObjectName(getID(name)));
	std::auto_ptr<RaidFileRead> r(RaidFileRead::Open(discSetNum, fn));
	RaidFileWrite w(discSetNum, fn);
	w.Open(true /* allow overwrite */);
	// Copy beginning
	char buf[2048];
	r->Read(buf, start, IOStream::TimeOutInfinite);
	w.Write(buf, start);
	// Write rubbish
	r->Seek(rubbish_len, IOStream::SeekType_Relative);
	w.Write(rubbish, rubbish_len);
	// Copy rest of file
	r->CopyStreamTo(w);
	r->Close();
	// Commit
	w.Commit(true /* convert now */);
}

BackupStoreFilename fnames[3];

typedef struct
{
	int name;
	int64_t id;
	int flags;
} dir_en_check;

void check_dir(BackupStoreDirectory &dir, dir_en_check *ck)
{
	BackupStoreDirectory::Iterator i(dir);
	BackupStoreDirectory::Entry *en;
	
	while((en = i.Next()) != 0)
	{
		BackupStoreFilenameClear clear(en->GetName());
		TEST_LINE(ck->name != -1, "Unexpected entry found in "
			"directory: " << clear.GetClearFilename());
		if(ck->name == -1)
		{
			break;
		}
		TEST_THAT(en->GetName() == fnames[ck->name]);
		TEST_THAT(en->GetObjectID() == ck->id);
		TEST_THAT(en->GetFlags() == ck->flags);
		++ck;
	}
	
	TEST_THAT(en == 0);
	TEST_THAT(ck->name == -1);
}

typedef struct
{
	int64_t id, depNewer, depOlder;
} checkdepinfoen;

void check_dir_dep(BackupStoreDirectory &dir, checkdepinfoen *ck)
{
	BackupStoreDirectory::Iterator i(dir);
	BackupStoreDirectory::Entry *en;
	
	while((en = i.Next()) != 0)
	{
		TEST_THAT(ck->id != -1);
		if(ck->id == -1)
		{
			break;
		}
		TEST_EQUAL_LINE(ck->id, en->GetObjectID(), "Wrong object ID "
			"for " << BOX_FORMAT_OBJECTID(ck->id));
		TEST_EQUAL_LINE(ck->depNewer, en->GetDependsNewer(),
			"Wrong Newer dependency for " << BOX_FORMAT_OBJECTID(ck->id));
		TEST_EQUAL_LINE(ck->depOlder, en->GetDependsOlder(),
			"Wrong Older dependency for " << BOX_FORMAT_OBJECTID(ck->id));
		++ck;
	}
	
	TEST_THAT(en == 0);
	TEST_THAT(ck->id == -1);
}

void test_dir_fixing()
{
	{
		MEMLEAKFINDER_NO_LEAKS;
		fnames[0].SetAsClearFilename("x1");
		fnames[1].SetAsClearFilename("x2");
		fnames[2].SetAsClearFilename("x3");
	}

	{
		BackupStoreDirectory dir;
		dir.AddEntry(fnames[0], 12, 2 /* id */, 1, BackupStoreDirectory::Entry::Flags_File, 2);
		dir.AddEntry(fnames[1], 12, 2 /* id */, 1, BackupStoreDirectory::Entry::Flags_File, 2);
		dir.AddEntry(fnames[0], 12, 3 /* id */, 1, BackupStoreDirectory::Entry::Flags_File, 2);
		dir.AddEntry(fnames[0], 12, 5 /* id */, 1, BackupStoreDirectory::Entry::Flags_File | BackupStoreDirectory::Entry::Flags_OldVersion, 2);
		
		dir_en_check ck[] = {
			{1, 2, BackupStoreDirectory::Entry::Flags_File},
			{0, 3, BackupStoreDirectory::Entry::Flags_File | BackupStoreDirectory::Entry::Flags_OldVersion},
			{0, 5, BackupStoreDirectory::Entry::Flags_File},
			{-1, 0, 0}
		};
		
		TEST_THAT(dir.CheckAndFix() == true);
		TEST_THAT(dir.CheckAndFix() == false);
		check_dir(dir, ck);
	}

	{
		BackupStoreDirectory dir;
		dir.AddEntry(fnames[0], 12, 2 /* id */, 1, BackupStoreDirectory::Entry::Flags_File, 2);
		dir.AddEntry(fnames[1], 12, 10 /* id */, 1, BackupStoreDirectory::Entry::Flags_File | BackupStoreDirectory::Entry::Flags_Dir | BackupStoreDirectory::Entry::Flags_OldVersion, 2);
		dir.AddEntry(fnames[0], 12, 3 /* id */, 1, BackupStoreDirectory::Entry::Flags_File | BackupStoreDirectory::Entry::Flags_OldVersion, 2);
		dir.AddEntry(fnames[0], 12, 5 /* id */, 1, BackupStoreDirectory::Entry::Flags_File | BackupStoreDirectory::Entry::Flags_OldVersion, 2);
		
		dir_en_check ck[] = {
			{0, 2, BackupStoreDirectory::Entry::Flags_File | BackupStoreDirectory::Entry::Flags_OldVersion},
			{1, 10, BackupStoreDirectory::Entry::Flags_Dir},
			{0, 3, BackupStoreDirectory::Entry::Flags_File | BackupStoreDirectory::Entry::Flags_OldVersion},
			{0, 5, BackupStoreDirectory::Entry::Flags_File},
			{-1, 0, 0}
		};
		
		TEST_THAT(dir.CheckAndFix() == true);
		TEST_THAT(dir.CheckAndFix() == false);
		check_dir(dir, ck);
	}

	// Test dependency fixing
	{
		BackupStoreDirectory dir;
		BackupStoreDirectory::Entry *e2
		  = dir.AddEntry(fnames[0], 12, 2 /* id */, 1, BackupStoreDirectory::Entry::Flags_File | BackupStoreDirectory::Entry::Flags_OldVersion, 2);
		TEST_THAT(e2 != 0);
		e2->SetDependsNewer(3);
		BackupStoreDirectory::Entry *e3
		  = dir.AddEntry(fnames[0], 12, 3 /* id */, 1, BackupStoreDirectory::Entry::Flags_File | BackupStoreDirectory::Entry::Flags_OldVersion, 2);
		TEST_THAT(e3 != 0);
		e3->SetDependsNewer(4); e3->SetDependsOlder(2);
		BackupStoreDirectory::Entry *e4
		  = dir.AddEntry(fnames[0], 12, 4 /* id */, 1, BackupStoreDirectory::Entry::Flags_File | BackupStoreDirectory::Entry::Flags_OldVersion, 2);
		TEST_THAT(e4 != 0);
		e4->SetDependsNewer(5); e4->SetDependsOlder(3);
		BackupStoreDirectory::Entry *e5
		  = dir.AddEntry(fnames[0], 12, 5 /* id */, 1, BackupStoreDirectory::Entry::Flags_File, 2);
		TEST_THAT(e5 != 0);
		e5->SetDependsOlder(4);
		
		// This should all be nice and valid
		TEST_THAT(dir.CheckAndFix() == false);
		static checkdepinfoen c1[] = {{2, 3, 0}, {3, 4, 2}, {4, 5, 3}, {5, 0, 4}, {-1, 0, 0}};
		check_dir_dep(dir, c1);

		// Check that dependency forwards are restored
		e4->SetDependsOlder(34343);
		TEST_THAT(dir.CheckAndFix() == true);
		TEST_THAT(dir.CheckAndFix() == false);
		check_dir_dep(dir, c1);

		// Check that a spurious depends older ref is undone
		e2->SetDependsOlder(1);
		TEST_THAT(dir.CheckAndFix() == true);
		TEST_THAT(dir.CheckAndFix() == false);
		check_dir_dep(dir, c1);

		// Now delete an entry, and check it's cleaned up nicely
		dir.DeleteEntry(3);
		TEST_THAT(dir.CheckAndFix() == true);
		TEST_THAT(dir.CheckAndFix() == false);
		static checkdepinfoen c2[] = {{4, 5, 0}, {5, 0, 4}, {-1, 0, 0}};
		check_dir_dep(dir, c2);
	}
}

int64_t fake_upload(BackupProtocolLocal& client, const std::string& file_path,
	int64_t diff_from_id)
{
	std::auto_ptr<IOStream> upload;
	if(diff_from_id)
	{
		std::auto_ptr<BackupProtocolSuccess> getBlockIndex(
			client.QueryGetBlockIndexByName(
				BACKUPSTORE_ROOT_DIRECTORY_ID, fnames[0]));
		std::auto_ptr<IOStream> blockIndexStream(client.ReceiveStream());
		upload = BackupStoreFile::EncodeFileDiff(
			file_path,
			BACKUPSTORE_ROOT_DIRECTORY_ID, fnames[0],
			diff_from_id, *blockIndexStream,
			IOStream::TimeOutInfinite,
			NULL, // DiffTimer implementation
			0 /* not interested in the modification time */, 
			NULL // isCompletelyDifferent
			);
	}
	else
	{
		upload = BackupStoreFile::EncodeFile(
			file_path,
			BACKUPSTORE_ROOT_DIRECTORY_ID, fnames[0],
			NULL, 
			NULL, // pLogger
			NULL // pRunStatusProvider
			);
	}

	return client.QueryStoreFile(BACKUPSTORE_ROOT_DIRECTORY_ID,
		1, // ModificationTime
		2, // AttributesHash
		diff_from_id, // DiffFromFileID
		fnames[0], // rFilename
		upload)->GetObjectID();
}

int test(int argc, const char *argv[])
{
	{
		MEMLEAKFINDER_NO_LEAKS;
		fnames[0].SetAsClearFilename("x1");
		fnames[1].SetAsClearFilename("x2");
		fnames[2].SetAsClearFilename("x3");
	}

	// Test the backupstore directory fixing
	test_dir_fixing();

	// Initialise the raidfile controller
	RaidFileController &rcontroller = RaidFileController::GetController();
	rcontroller.Initialise("testfiles/raidfile.conf");
	BackupClientCryptoKeys_Setup("testfiles/bbackupd.keys");

	// Create an account
	TEST_THAT_ABORTONFAIL(::system(BBSTOREACCOUNTS 
		" -c testfiles/bbstored.conf "
		"create 01234567 0 10000B 20000B") == 0);
	TestRemoteProcessMemLeaks("bbstoreaccounts.memleaks");

	// Start the bbstored server
	int bbstored_pid = LaunchServer(BBSTORED " testfiles/bbstored.conf", 
		"testfiles/bbstored.pid");
	TEST_THAT(bbstored_pid > 0);
	if (bbstored_pid <= 0) return 1;
	
	::sleep(1);
	TEST_THAT(ServerIsAlive(bbstored_pid));

	// Run the perl script to create the initial directories
	TEST_THAT_ABORTONFAIL(::system(PERL_EXECUTABLE 
		" testfiles/testbackupstorefix.pl init") == 0);

	std::string cmd = BBACKUPD " " + bbackupd_args +
		" testfiles/bbackupd.conf";
	int bbackupd_pid = LaunchServer(cmd, "testfiles/bbackupd.pid");
	TEST_THAT(bbackupd_pid > 0);
	if (bbackupd_pid <= 0) return 1;

	::safe_sleep(1);
	TEST_THAT(ServerIsAlive(bbackupd_pid));

	// Wait 4 more seconds for the files to be old enough
	// to upload
	::safe_sleep(4);

	// Upload files to create a nice store directory
	::sync_and_wait();

	// Stop bbackupd
	#ifdef WIN32
		terminate_bbackupd(bbackupd_pid);
		// implicit check for memory leaks
	#else
		TEST_THAT(KillServer(bbackupd_pid));
		TestRemoteProcessMemLeaks("bbackupd.memleaks");
	#endif

	// Add a reference to a file that doesn't exist, check that it's removed
	{
		std::string fn;
		StoreStructure::MakeObjectFilename(1 /* root */, storeRoot,
			discSetNum, fn, true /* EnsureDirectoryExists */);

		std::auto_ptr<RaidFileRead> file(RaidFileRead::Open(discSetNum,
			fn));
		BackupStoreDirectory dir;
		dir.ReadFromStream(*file, IOStream::TimeOutInfinite);
		
		dir.AddEntry(fnames[0], 12, 0x1234567890123456LL /* id */, 1,
			BackupStoreDirectory::Entry::Flags_File, 2);
		
		RaidFileWrite d(discSetNum, fn);
		d.Open(true /* allow overwrite */);
		dir.WriteToStream(d);
		d.Commit(true /* write now! */);

		file = RaidFileRead::Open(discSetNum, fn);
		dir.ReadFromStream(*file, IOStream::TimeOutInfinite);
		TEST_THAT(dir.FindEntryByID(0x1234567890123456LL) != 0);

		// Check it
		BackupStoreCheck checker(storeRoot, discSetNum,
			0x01234567, true /* FixErrors */, false /* Quiet */);
		checker.Check();
		TEST_EQUAL(1, checker.GetNumErrorsFound());

		file = RaidFileRead::Open(discSetNum, fn);
		dir.ReadFromStream(*file, IOStream::TimeOutInfinite);
		TEST_THAT(dir.FindEntryByID(0x1234567890123456LL) == 0);
	}

	if (failures > 0) return 1;
	
	// Generate a list of all the object IDs
	TEST_THAT_ABORTONFAIL(::system(BBACKUPQUERY " -Wwarning "
		"-c testfiles/bbackupd.conf \"list -r\" quit "
		"> testfiles/initial-listing.txt") == 0);

	// And load it in
	{
		FILE *f = ::fopen("testfiles/initial-listing.txt", "r");
		TEST_THAT_ABORTONFAIL(f != 0);
		char line[512];
		int32_t id;
		char flags[32];
		char name[256];
		while(::fgets(line, sizeof(line), f) != 0)
		{
			TEST_THAT(::sscanf(line, "%x %s %s", &id, 
				flags, name) == 3);
			bool isDir = (::strcmp(flags, "-d---") == 0);
			//TRACE3("%x,%d,%s\n", id, isDir, name);
			MEMLEAKFINDER_NO_LEAKS;
			nameToID[std::string(name)] = id;
			objectIsDir[id] = isDir;
		}
		::fclose(f);
	}

	// ------------------------------------------------------------------------------------------------		
	::printf("  === Delete store info, add random file\n");
	{
		// Delete store info
		RaidFileWrite del(discSetNum, storeRoot + "info");
		del.Delete();
	}
	{
		// Add a spurious file
		RaidFileWrite random(discSetNum, 
			storeRoot + "randomfile");
		random.Open();
		random.Write("test", 4);
		random.Commit(true);
	}

	// Fix it
	RUN_CHECK

	// Check everything is as it was
	TEST_THAT(::system(PERL_EXECUTABLE 
		" testfiles/testbackupstorefix.pl check 0") == 0);
	// Check the random file doesn't exist
	{
		TEST_THAT(!RaidFileRead::FileExists(discSetNum, 
			storeRoot + "01/randomfile"));
	}

	// ------------------------------------------------------------------------------------------------		
	::printf("  === Delete an entry for an object from dir, change that object to be a patch, check it's deleted\n");
	{
		// Open dir and find entry
		int64_t delID = getID("Test1/cannes/ict/metegoguered/oats");
		{
			BackupStoreDirectory dir;
			LoadDirectory("Test1/cannes/ict/metegoguered", dir);
			TEST_THAT(dir.FindEntryByID(delID) != 0);
			dir.DeleteEntry(delID);
			SaveDirectory("Test1/cannes/ict/metegoguered", dir);
		}
		
		// Adjust that entry
		//
		// IMPORTANT NOTE: There's a special hack in testbackupstorefix.pl to make sure that
		// the file we're modifiying has at least two blocks so we can modify it and produce a valid file
		// which will pass the verify checks.
		//
		std::string fn(getObjectName(delID));
		{
			std::auto_ptr<RaidFileRead> file(RaidFileRead::Open(discSetNum, fn));
			RaidFileWrite f(discSetNum, fn);
			f.Open(true /* allow overwrite */);
			// Make a copy of the original
			file->CopyStreamTo(f);
			// Move to header in both
			file->Seek(0, IOStream::SeekType_Absolute);
			BackupStoreFile::MoveStreamPositionToBlockIndex(*file);
			f.Seek(file->GetPosition(), IOStream::SeekType_Absolute);
			// Read header
			struct
			{
				file_BlockIndexHeader hdr;
				file_BlockIndexEntry e[2];
			} h;
			TEST_THAT(file->Read(&h, sizeof(h)) == sizeof(h));
			file->Close();

			// Modify
			TEST_THAT(box_ntoh64(h.hdr.mOtherFileID) == 0);
			TEST_THAT(box_ntoh64(h.hdr.mNumBlocks) >= 2);
			h.hdr.mOtherFileID = box_hton64(2345); // don't worry about endianness
			h.e[0].mEncodedSize = box_hton64((box_ntoh64(h.e[0].mEncodedSize)) + (box_ntoh64(h.e[1].mEncodedSize)));
			h.e[1].mOtherBlockIndex = box_hton64(static_cast<uint64_t>(-2));
			// Write to modified file
			f.Write(&h, sizeof(h));
			// Commit new version
			f.Commit(true /* write now! */);
		}

		// Fix it
		RUN_CHECK
		// Check
		TEST_THAT(::system(PERL_EXECUTABLE 
			" testfiles/testbackupstorefix.pl check 1") 
			== 0);

		// Check the modified file doesn't exist
		TEST_THAT(!RaidFileRead::FileExists(discSetNum, fn));
	}
	
	// ------------------------------------------------------------------------------------------------		
	::printf("  === Delete directory, change container ID of another, duplicate entry in dir, spurious file size, delete file\n");
	{
		BackupStoreDirectory dir;
		LoadDirectory("Test1/foreomizes/stemptinevidate/ict", dir);
		dir.SetContainerID(73773);
		SaveDirectory("Test1/foreomizes/stemptinevidate/ict", dir);
	}
	int64_t duplicatedID = 0;
	int64_t notSpuriousFileSize = 0;
	{
		BackupStoreDirectory dir;
		LoadDirectory("Test1/cannes/ict/peep", dir);
		// Duplicate the second entry
		{
			BackupStoreDirectory::Iterator i(dir);
			i.Next();
			BackupStoreDirectory::Entry *en = i.Next();
			TEST_THAT(en != 0);
			duplicatedID = en->GetObjectID();
			dir.AddEntry(*en);
		}
		// Adjust file size of first file
		{
			BackupStoreDirectory::Iterator i(dir);
			BackupStoreDirectory::Entry *en = i.Next(BackupStoreDirectory::Entry::Flags_File);
			TEST_THAT(en != 0);
			notSpuriousFileSize = en->GetSizeInBlocks();
			en->SetSizeInBlocks(3473874);
			TEST_THAT(en->GetSizeInBlocks() == 3473874);
		}
		SaveDirectory("Test1/cannes/ict/peep", dir);
	}
	// Delete a directory
	DeleteObject("Test1/pass/cacted/ming");
	// Delete a file
	DeleteObject("Test1/cannes/ict/scely");
	// Fix it
	RUN_CHECK
	// Check everything is as it should be
	TEST_THAT(::system(PERL_EXECUTABLE 
		" testfiles/testbackupstorefix.pl check 2") == 0);
	{
		BackupStoreDirectory dir;
		LoadDirectory("Test1/foreomizes/stemptinevidate/ict", dir);
		TEST_THAT(dir.GetContainerID() == getID("Test1/foreomizes/stemptinevidate"));
	}
	{
		BackupStoreDirectory dir;
		LoadDirectory("Test1/cannes/ict/peep", dir);
		BackupStoreDirectory::Iterator i(dir);
		// Count the number of entries with the ID which was duplicated
		int count = 0;
		BackupStoreDirectory::Entry *en = 0;
		while((en = i.Next()) != 0)
		{
			if(en->GetObjectID() == duplicatedID)
			{
				++count;
			}
		}
		TEST_THAT(count == 1);
		// Check file size has changed
		{
			BackupStoreDirectory::Iterator i(dir);
			BackupStoreDirectory::Entry *en = i.Next(BackupStoreDirectory::Entry::Flags_File);
			TEST_THAT(en != 0);
			TEST_THAT(en->GetSizeInBlocks() == notSpuriousFileSize);
		}
	}

	// ------------------------------------------------------------------------------------------------		
	::printf("  === Modify the obj ID of dir, delete dir with no members, add extra reference to a file\n");
	// Set bad object ID
	{
		BackupStoreDirectory dir;
		LoadDirectory("Test1/foreomizes/stemptinevidate/ict", dir);
		dir.TESTONLY_SetObjectID(73773);
		SaveDirectory("Test1/foreomizes/stemptinevidate/ict", dir);
	}
	// Delete dir with no members
	DeleteObject("Test1/dir-no-members");
	// Add extra reference
	{
		BackupStoreDirectory dir;
		LoadDirectory("Test1/divel", dir);
		BackupStoreDirectory::Iterator i(dir);
		BackupStoreDirectory::Entry *en = i.Next(BackupStoreDirectory::Entry::Flags_File);
		TEST_THAT(en != 0);
		BackupStoreDirectory dir2;
		LoadDirectory("Test1/divel/torsines/cruishery", dir2);
		dir2.AddEntry(*en);
		SaveDirectory("Test1/divel/torsines/cruishery", dir2);
	}
	// Fix it
	RUN_CHECK
	// Check everything is as it should be
	TEST_THAT(::system(PERL_EXECUTABLE 
		" testfiles/testbackupstorefix.pl check 3") == 0);
	{
		BackupStoreDirectory dir;
		LoadDirectory("Test1/foreomizes/stemptinevidate/ict", dir);
		TEST_THAT(dir.GetObjectID() == getID("Test1/foreomizes/stemptinevidate/ict"));
	}
	
	// ------------------------------------------------------------------------------------------------		
	::printf("  === Orphan files and dirs without being recoverable\n");
	DeleteObject("Test1/dir1");		
	DeleteObject("Test1/dir1/dir2");		
	// Fix it
	RUN_CHECK
	// Check everything is where it is predicted to be
	TEST_THAT(::system(PERL_EXECUTABLE 
		" testfiles/testbackupstorefix.pl check 4") == 0);

	// ------------------------------------------------------------------------------------------------		
	::printf("  === Corrupt file and dir\n");
	// File
	CorruptObject("Test1/foreomizes/stemptinevidate/algoughtnerge",
		33, "34i729834298349283479233472983sdfhasgs");
	// Dir
	CorruptObject("Test1/cannes/imulatrougge/foreomizes",23, 
		"dsf32489sdnadf897fd2hjkesdfmnbsdfcsfoisufio2iofe2hdfkjhsf");
	// Fix it
	RUN_CHECK
	// Check everything is where it should be
	TEST_THAT(::system(PERL_EXECUTABLE 
		" testfiles/testbackupstorefix.pl check 5") == 0);

	// ------------------------------------------------------------------------------------------------		
	::printf("  === Overwrite root with a file\n");
	{
		std::auto_ptr<RaidFileRead> r(RaidFileRead::Open(discSetNum, getObjectName(getID("Test1/pass/shuted/brightinats/milamptimaskates"))));
		RaidFileWrite w(discSetNum, getObjectName(1 /* root */));
		w.Open(true /* allow overwrite */);
		r->CopyStreamTo(w);
		w.Commit(true /* convert now */);
	}
	// Fix it
	RUN_CHECK
	// Check everything is where it should be
	TEST_THAT(::system(PERL_EXECUTABLE 
		" testfiles/testbackupstorefix.pl reroot 6") == 0);


	// ---------------------------------------------------------
	// Stop server
	TEST_THAT(KillServer(bbstored_pid));

	#ifdef WIN32
		TEST_THAT(unlink("testfiles/bbstored.pid") == 0);
	#else
		TestRemoteProcessMemLeaks("bbstored.memleaks");
	#endif

	return 0;
}

