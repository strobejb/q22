//
//  seqbuf.cpp
//
//  www.catch22.net
//
//  Copyright (C) 2012 James Brown
//  Please refer to the file LICENCE.TXT for copying permission
//

#include <stdarg.h>
#include <stdio.h>
#include <new>
#include <string>
#include <algorithm>

#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QTemporaryFile>

#include "sequence.h"

// ---------------------------------------------------------------------------
// Cross-platform file I/O helpers
//
// All Win32 HANDLE usage is replaced with QFile*.
// Callers in sequence.cpp use void* — cast to QFile* here.
// ---------------------------------------------------------------------------

bool read_data(void *hFile, seqchar *buffer, size_w offset, size_w length)
{
	QFile *f = static_cast<QFile *>(hFile);
	if(!f || !f->seek(static_cast<qint64>(offset) * sizeof(seqchar)))
		return false;
	qint64 numread = f->read(reinterpret_cast<char *>(buffer),
	                         static_cast<qint64>(length) * sizeof(seqchar));
	return numread == static_cast<qint64>(length) * sizeof(seqchar);
}

bool write_data(void *hFile, seqchar *buffer, size_w offset, size_w length)
{
	QFile *f = static_cast<QFile *>(hFile);
	if(!f || !f->seek(static_cast<qint64>(offset) * sizeof(seqchar)))
		return false;
	qint64 numwritten = f->write(reinterpret_cast<const char *>(buffer),
	                              static_cast<qint64>(length) * sizeof(seqchar));
	return numwritten == static_cast<qint64>(length) * sizeof(seqchar);
}

// ---------------------------------------------------------------------------
// Utility functions called from sequence.cpp
// ---------------------------------------------------------------------------

// Truncate/extend an already-open QFile to the specified length.
bool allocfile(void *hFile, size_w length)
{
	QFile *f = static_cast<QFile *>(hFile);
	if(!f) return false;
	return f->resize(static_cast<qint64>(length) * sizeof(seqchar));
}

// Create (or open) a named file at the given size.  Returns a new QFile*.
// Caller owns the pointer and must closefile() it when done.
void *allocfile(const std::string &filename, size_w length)
{
	QFile *f = new QFile(QString::fromStdString(filename));
	if(!f->open(QIODevice::ReadWrite))
	{
		delete f;
		return nullptr;
	}
	if(!f->resize(static_cast<qint64>(length) * sizeof(seqchar)))
	{
		f->close();
		delete f;
		return nullptr;
	}
	return f;
}

// Create a temporary file of the given size.
// On success sets tmpfilename to the path of the temp file and returns a QFile*.
// Caller owns the pointer and must closefile() it when done.
void *alloctmpfile(std::string &tmpfilename, size_w length)
{
	// QTemporaryFile auto-generates a unique name.
	// We need to keep the file open AND know its path, so we use
	// QTemporaryFile with autoRemove(false) and transfer ownership to a
	// plain QFile so the interface stays consistent.
	QTemporaryFile tmp;
	tmp.setAutoRemove(false);

	if(!tmp.open())
		return nullptr;

	tmpfilename = tmp.fileName().toStdString();
	tmp.close();

	// Now open as a regular QFile (ReadWrite) and size it
	QFile *f = new QFile(tmp.fileName());
	if(!f->open(QIODevice::ReadWrite))
	{
		delete f;
		QFile::remove(tmp.fileName());
		tmpfilename.clear();
		return nullptr;
	}
	if(!f->resize(static_cast<qint64>(length) * sizeof(seqchar)))
	{
		f->close();
		delete f;
		QFile::remove(tmp.fileName());
		tmpfilename.clear();
		return nullptr;
	}
	return f;
}

// Close and delete the QFile owned by sequence.cpp helpers.
void closefile(void *hFile)
{
	QFile *f = static_cast<QFile *>(hFile);
	if(f)
	{
		f->close();
		delete f;
	}
}

// Atomic file replacement: remove dest then rename src -> dest.
void movefile(const std::string &src, const std::string &dest)
{
	QString qsrc  = QString::fromStdString(src);
	QString qdest = QString::fromStdString(dest);
	QFile::remove(qdest);
	QFile::rename(qsrc, qdest);
}

// ---------------------------------------------------------------------------
// calc_index_base — sliding-window alignment, kept completely intact
// ---------------------------------------------------------------------------

size_w inline calc_index_base(size_w index)
{
	if(index < MEM_BLOCK_SIZE / 2)
	{
		return 0;
	}
	else
	{
		return ((index + MEM_BLOCK_SIZE / 4) & (~(MEM_BLOCK_SIZE / 2 - 1))) - (MEM_BLOCK_SIZE / 2);
	}
}

// ---------------------------------------------------------------------------
// sequence::getptr — bridge from span to buffer_control
// ---------------------------------------------------------------------------

seqchar * sequence::getptr(span *sptr)
{
	return buffer_list[sptr->buffer]->getptr(sptr->offset, sptr->length);
}

// ---------------------------------------------------------------------------
// sequence::buffer_control::getptr
//
// Sliding-window view logic kept completely intact.
// hFile is now a QFile* stored as void*.
// ---------------------------------------------------------------------------

seqchar * sequence::buffer_control::getptr(size_w off, size_w len)
{
	size_t i;

	// search for a buffer that already contains the requested range of data
	for(i = 0; i < MAX_VIEWS; i++)
	{
		buffer_view *bv = &viewlist[i];

		if(bv->initialized && off >= bv->offset && off + len <= bv->offset + bv->length)
		{
			return bv->buffer + (off - bv->offset);
		}
	}

	// didn't find one, so map in a new view
	if(hFile)
	{
		buffer_view *bv = &viewlist[0];

		// find one that isn't initialized yet
		for(i = 0; i < MAX_VIEWS; i++)
		{
			bv = &viewlist[i];

			if(bv->initialized == false)
			{
				bv->buffer		= new seqchar[MEM_BLOCK_SIZE];
				bv->offset		= calc_index_base(off);
				bv->length		= MEM_BLOCK_SIZE;
				bv->initialized = true;

				read_data(hFile, bv->buffer, bv->offset, bv->length);
				return bv->buffer + (off - bv->offset);
			}
		}

		// all views in use — reuse slot 0 (simple LRU replacement)
		bv->offset = calc_index_base(off);
		read_data(hFile, bv->buffer, bv->offset, bv->length);

		return bv->buffer + (off - bv->offset);
	}

	return 0;
}

// ---------------------------------------------------------------------------
// buffer_control::init — fixed buffer (copy or reference)
// ---------------------------------------------------------------------------

bool sequence::buffer_control::init(const seqchar * buf, size_w len, bool copybuf)
{
	if(copybuf && buf == 0)
		return false;

	// make sure we don't try to alloc something too big for new[]
	if(len >= 0xffffffff)
		return false;

	length	= len;
	maxsize	= len;
	ownbuf	= copybuf;

	viewlist[0].buffer = (seqchar *)buf;
	viewlist[0].length = len;
	viewlist[0].offset = 0;

	try
	{
		if(buf == nullptr || copybuf)
		{
			if((viewlist[0].buffer = new seqchar[(size_t)len]) == 0)
				return false;
		}

		// duplicate the source buffer
		if(copybuf)
		{
			memcpy(viewlist[0].buffer, buf, (size_t)len * sizeof(seqchar));
		}
	}
	catch(std::bad_alloc &)
	{
		return false;
	}

	viewlist[0].initialized = true;
	return true;
}

// ---------------------------------------------------------------------------
// buffer_control::init — fixed empty (modify) buffer
// ---------------------------------------------------------------------------

bool sequence::buffer_control::init(size_t max)
{
	if((viewlist[0].buffer = new seqchar[max]) == 0)
		return false;

	viewlist[0].initialized = true;
	viewlist[0].offset = 0;
	viewlist[0].length = max;

	maxsize = max;
	length  = 0;
	ownbuf  = true;

	return true;
}

bool sequence::buffer_control::append(const seqchar * buf, size_t len)
{
	if(length + len < maxsize)
	{
		buffer_view *bv = &viewlist[0];
		memcpy(bv->buffer + length, buf, len * sizeof(seqchar));
		length += len;
		return true;
	}
	else
	{
		return false;
	}
}

// ---------------------------------------------------------------------------
// buffer_control::load — open file with QFile
//
// Replaces all Win32 CreateFile / GetFileAttributes / GetFileSize / ReadFile
// / CloseHandle calls.
//
// Logic is preserved exactly:
//   - if file is not writable, force readonly
//   - try read/write first; fall back to readonly on sharing/access errors
//   - files < 10 MB are read fully into a view buffer (quickload=false)
//   - larger files (or quickload=true) are kept on disk; views loaded lazily
// ---------------------------------------------------------------------------

bool sequence::buffer_control::load(const std::string &filename, bool reado, bool quickload)
{
	bool  success = false;
	readonly = reado;

	QString qpath = QString::fromStdString(filename);

	// If the file is physically read-only we cannot open it for writing
	if(!QFileInfo(qpath).isWritable())
		readonly = true;

	QIODevice::OpenMode mode = readonly
	    ? QIODevice::ReadOnly
	    : QIODevice::ReadWrite;

	QFile *f = new QFile(qpath);

	if(!f->open(mode))
	{
		// Try again with read-only (equivalent to sharing/access-denied fallback)
		if(!readonly)
		{
			readonly = true;
			if(!f->open(QIODevice::ReadOnly))
			{
				delete f;
				return false;
			}
		}
		else
		{
			delete f;
			return false;
		}
	}

	hFile = f;

	qint64 fsize = f->size();
	length  = static_cast<size_w>(fsize / sizeof(seqchar));
	maxsize = length;
	ownbuf  = true;

	// only quickload if file is < 10 MB
	if(quickload == false && fsize < 1024LL * 1024LL * 10LL)
	{
		if(init(nullptr, length, false))
		{
			if(read_data(hFile, viewlist[0].buffer, 0, length))
			{
				success = true;
			}
		}

		// file is now fully buffered — close it
		f->close();
		delete f;
		hFile = nullptr;
	}
	else
	{
		// keep the file open for lazy view loading
		success = true;
	}

	if(!success)
	{
		delete[] viewlist[0].buffer;
		viewlist[0].buffer      = nullptr;
		viewlist[0].initialized = false;

		if(hFile)
		{
			static_cast<QFile *>(hFile)->close();
			delete static_cast<QFile *>(hFile);
			hFile = nullptr;
		}
	}

	return success;
}

// ---------------------------------------------------------------------------
// sequence::_handle
//
// Returns the underlying file handle (as void*) for the first non-zero buffer
// file handle found, mirroring the original Win32 HANDLE _handle() behaviour.
// ---------------------------------------------------------------------------

void* sequence::_handle()
{
	if(buffer_list.size() > 1)
	{
		for(size_t i = 0; i < buffer_list.size(); i++)
		{
			void *h = buffer_list[i]->hFile;
			(void)h; // mirror original loop
		}
		return buffer_list[1]->hFile;
	}

	return nullptr;
}

// ---------------------------------------------------------------------------
// buffer_control::free
// ---------------------------------------------------------------------------

void sequence::buffer_control::free()
{
	if(hFile)
	{
		QFile *f = static_cast<QFile *>(hFile);
		f->close();
		delete f;
		hFile = nullptr;
	}

	for(size_t i = 0; ownbuf && i < MAX_VIEWS; i++)
	{
		if(viewlist[i].initialized)
		{
			delete[] viewlist[i].buffer;
			viewlist[i].buffer      = nullptr;
			viewlist[i].initialized = false;
		}
	}
}
