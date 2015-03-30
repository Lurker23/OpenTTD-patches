/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file base_media_func.h Generic function implementations for base data (graphics, sounds).
 */

#include "base_media_base.h"
#include "debug.h"
#include "ini_type.h"
#include "string.h"

template <class Tbase_set> /* static */ const char *BaseMedia<Tbase_set>::ini_set;
template <class Tbase_set> /* static */ const Tbase_set *BaseMedia<Tbase_set>::used_set;
template <class Tbase_set> /* static */ Tbase_set *BaseMedia<Tbase_set>::available_sets;
template <class Tbase_set> /* static */ Tbase_set *BaseMedia<Tbase_set>::duplicate_sets;

/**
 * Try to read a single piece of metadata.
 * @param metadata the metadata group to search in.
 * @param name the name of the item to fetch.
 * @param filename the name of the filename for debugging output.
 * @return the associated item, or NULL if it doesn't exist.
 */
template <typename T>
static inline const IniItem *fetch_metadata (const IniGroup *metadata,
	const char *name, const char *filename)
{
	const IniItem *item = metadata->find (name);
	if (item == NULL || StrEmpty (item->value)) {
		DEBUG (grf, 0, "Base %sset detail loading: %s field missing in %s.",
				T::set_type, name, filename);
		return NULL;
	}
	return item;
}

/**
 * Read the set information from a loaded ini.
 * @param ini      the ini to read from
 * @param path     the path to this ini file (for filenames)
 * @param full_filename the full filename of the loaded file (for error reporting purposes)
 * @param allow_empty_filename empty filenames are valid
 * @return true if loading was successful.
 */
template <class T, size_t Tnum_files, bool Tsearch_in_tars>
bool BaseSet<T, Tnum_files, Tsearch_in_tars>::FillSetDetails(IniFile *ini, const char *path, const char *full_filename, bool allow_empty_filename)
{
	memset(this, 0, sizeof(*this));

	const IniGroup *metadata = ini->find ("metadata");
	if (metadata == NULL) {
		DEBUG (grf, 0, "Base %sset detail loading: metadata group missing.", T::set_type);
		DEBUG (grf, 0, "  Is %s readable for the user running OpenTTD?", full_filename);
		return false;
	}

	const IniItem *item;

	item = fetch_metadata<T> (metadata, "name", full_filename);
	if (item == NULL) return false;
	this->name = xstrdup(item->value);

	item = fetch_metadata<T> (metadata, "description", full_filename);
	if (item == NULL) return false;
	this->description[xstrdup("")] = xstrdup(item->value);

	/* Add the translations of the descriptions too. */
	for (IniItem::const_iterator item = metadata->cbegin(); item != metadata->cend(); item++) {
		if (strncmp("description.", item->get_name(), 12) != 0) continue;

		this->description[xstrdup(item->get_name() + 12)] = xstrdup(item->value);
	}

	item = fetch_metadata<T> (metadata, "shortname", full_filename);
	if (item == NULL) return false;
	for (uint i = 0; item->value[i] != '\0' && i < 4; i++) {
		this->shortname |= ((uint8)item->value[i]) << (i * 8);
	}

	item = fetch_metadata<T> (metadata, "version", full_filename);
	if (item == NULL) return false;
	this->version = atoi(item->value);

	item = metadata->find ("fallback");
	this->fallback = (item != NULL && strcmp(item->value, "0") != 0 && strcmp(item->value, "false") != 0);

	/* For each of the file types we want to find the file, MD5 checksums and warning messages. */
	const IniGroup *files  = ini->get_group ("files");
	const IniGroup *md5s   = ini->get_group ("md5s");
	const IniGroup *origin = ini->get_group ("origin");
	for (uint i = 0; i < Tnum_files; i++) {
		MD5File *file = &this->files[i];
		/* Find the filename first. */
		item = files->find (BaseSet<T, Tnum_files, Tsearch_in_tars>::file_names[i]);
		if (item == NULL || (item->value == NULL && !allow_empty_filename)) {
			DEBUG(grf, 0, "No %s file for: %s (in %s)", T::set_type, BaseSet<T, Tnum_files, Tsearch_in_tars>::file_names[i], full_filename);
			return false;
		}

		const char *filename = item->value;
		if (filename == NULL) {
			file->filename = NULL;
			/* If we list no file, that file must be valid */
			this->valid_files++;
			this->found_files++;
			continue;
		}

		file->filename = str_fmt("%s%s", path, filename);

		/* Then find the MD5 checksum */
		item = md5s->find (filename);
		if (item == NULL || item->value == NULL) {
			DEBUG(grf, 0, "No MD5 checksum specified for: %s (in %s)", filename, full_filename);
			return false;
		}
		char *c = item->value;
		for (uint i = 0; i < sizeof(file->hash) * 2; i++, c++) {
			uint j;
			if ('0' <= *c && *c <= '9') {
				j = *c - '0';
			} else if ('a' <= *c && *c <= 'f') {
				j = *c - 'a' + 10;
			} else if ('A' <= *c && *c <= 'F') {
				j = *c - 'A' + 10;
			} else {
				DEBUG(grf, 0, "Malformed MD5 checksum specified for: %s (in %s)", filename, full_filename);
				return false;
			}
			if (i % 2 == 0) {
				file->hash[i / 2] = j << 4;
			} else {
				file->hash[i / 2] |= j;
			}
		}

		/* Then find the warning message when the file's missing */
		item = origin->find (filename);
		if (item == NULL) item = origin->find ("default");
		if (item == NULL) {
			DEBUG(grf, 1, "No origin warning message specified for: %s", filename);
			file->missing_warning = xstrdup("");
		} else {
			file->missing_warning = xstrdup(item->value);
		}

		switch (T::CheckMD5(file, BASESET_DIR)) {
			case MD5File::CR_MATCH:
				this->valid_files++;
				this->found_files++;
				break;

			case MD5File::CR_MISMATCH:
				DEBUG(grf, 1, "MD5 checksum mismatch for: %s (in %s)", filename, full_filename);
				this->found_files++;
				break;

			case MD5File::CR_NO_FILE:
				DEBUG(grf, 1, "The file %s specified in %s is missing", filename, full_filename);
				break;
		}
	}

	return true;
}

template <class Tbase_set>
bool BaseMedia<Tbase_set>::AddFile(const char *filename, size_t basepath_length, const char *tar_filename)
{
	bool ret = false;
	DEBUG(grf, 1, "Checking %s for base %s set", filename, Tbase_set::set_type);

	Tbase_set *set = new Tbase_set();
	IniFile *ini = new IniFile();
	ini->LoadFromDisk(filename, BASESET_DIR);

	char *path = xstrdup(filename + basepath_length);
	char *psep = strrchr(path, PATHSEPCHAR);
	if (psep != NULL) {
		psep[1] = '\0';
	} else {
		*path = '\0';
	}

	if (set->FillSetDetails(ini, path, filename)) {
		Tbase_set *duplicate = NULL;
		for (Tbase_set *c = BaseMedia<Tbase_set>::available_sets; c != NULL; c = c->next) {
			if (strcmp(c->name, set->name) == 0 || c->shortname == set->shortname) {
				duplicate = c;
				break;
			}
		}
		if (duplicate != NULL) {
			/* The more complete set takes precedence over the version number. */
			if ((duplicate->valid_files == set->valid_files && duplicate->version >= set->version) ||
					duplicate->valid_files > set->valid_files) {
				DEBUG(grf, 1, "Not adding %s (%i) as base %s set (duplicate, %s)",
						set->name, set->version, Tbase_set::set_type,
						duplicate->valid_files > set->valid_files ? "less valid files" : "lower version");
				set->next = BaseMedia<Tbase_set>::duplicate_sets;
				BaseMedia<Tbase_set>::duplicate_sets = set;
			} else {
				Tbase_set **prev = &BaseMedia<Tbase_set>::available_sets;
				while (*prev != duplicate) prev = &(*prev)->next;

				*prev = set;
				set->next = duplicate->next;

				/* If the duplicate set is currently used (due to rescanning this can happen)
				 * update the currently used set to the new one. This will 'lie' about the
				 * version number until a new game is started which isn't a big problem */
				if (BaseMedia<Tbase_set>::used_set == duplicate) BaseMedia<Tbase_set>::used_set = set;

				DEBUG(grf, 1, "Removing %s (%i) as base %s set (duplicate, %s)",
						duplicate->name, duplicate->version, Tbase_set::set_type,
						duplicate->valid_files < set->valid_files ? "less valid files" : "lower version");
				duplicate->next = BaseMedia<Tbase_set>::duplicate_sets;
				BaseMedia<Tbase_set>::duplicate_sets = duplicate;
				ret = true;
			}
		} else {
			Tbase_set **last = &BaseMedia<Tbase_set>::available_sets;
			while (*last != NULL) last = &(*last)->next;

			*last = set;
			ret = true;
		}
		if (ret) {
			DEBUG(grf, 1, "Adding %s (%i) as base %s set", set->name, set->version, Tbase_set::set_type);
		}
	} else {
		delete set;
	}
	free(path);

	delete ini;
	return ret;
}

/**
 * Set the set to be used.
 * @param name of the set to use
 * @return true if it could be loaded
 */
template <class Tbase_set>
/* static */ bool BaseMedia<Tbase_set>::SetSet(const char *name)
{
	extern void CheckExternalFiles();

	if (StrEmpty(name)) {
		if (!BaseMedia<Tbase_set>::DetermineBestSet()) return false;
		CheckExternalFiles();
		return true;
	}

	for (const Tbase_set *s = BaseMedia<Tbase_set>::available_sets; s != NULL; s = s->next) {
		if (strcmp(name, s->name) == 0) {
			BaseMedia<Tbase_set>::used_set = s;
			CheckExternalFiles();
			return true;
		}
	}
	return false;
}

/**
 * Returns a list with the sets.
 * @param buf where to print to
 */
template <class Tbase_set>
/* static */ void BaseMedia<Tbase_set>::GetSetsList (stringb *buf)
{
	buf->append_fmt ("List of %s sets:\n", Tbase_set::set_type);
	for (const Tbase_set *s = BaseMedia<Tbase_set>::available_sets; s != NULL; s = s->next) {
		buf->append_fmt ("%18s: %s", s->name, s->GetDescription());
		int invalid = s->GetNumInvalid();
		if (invalid != 0) {
			int missing = s->GetNumMissing();
			if (missing == 0) {
				buf->append_fmt (" (%i corrupt file%s)\n", invalid, invalid == 1 ? "" : "s");
			} else {
				buf->append_fmt (" (unusable: %i missing file%s)\n", missing, missing == 1 ? "" : "s");
			}
		} else {
			buf->append ('\n');
		}
	}
	buf->append ('\n');
}

#if defined(ENABLE_NETWORK)
#include "network/network_content.h"

template <class Tbase_set> const char *TryGetBaseSetFile(const ContentInfo *ci, bool md5sum, const Tbase_set *s)
{
	for (; s != NULL; s = s->next) {
		if (s->GetNumMissing() != 0) continue;

		if (s->shortname != ci->unique_id) continue;
		if (!md5sum) return  s->files[0].filename;

		byte md5[16];
		memset(md5, 0, sizeof(md5));
		for (uint i = 0; i < Tbase_set::NUM_FILES; i++) {
			for (uint j = 0; j < sizeof(md5); j++) {
				md5[j] ^= s->files[i].hash[j];
			}
		}
		if (memcmp(md5, ci->md5sum, sizeof(md5)) == 0) return s->files[0].filename;
	}
	return NULL;
}

template <class Tbase_set>
/* static */ bool BaseMedia<Tbase_set>::HasSet(const ContentInfo *ci, bool md5sum)
{
	return (TryGetBaseSetFile(ci, md5sum, BaseMedia<Tbase_set>::available_sets) != NULL) ||
			(TryGetBaseSetFile(ci, md5sum, BaseMedia<Tbase_set>::duplicate_sets) != NULL);
}

#else

template <class Tbase_set>
const char *TryGetBaseSetFile(const ContentInfo *ci, bool md5sum, const Tbase_set *s)
{
	return NULL;
}

template <class Tbase_set>
/* static */ bool BaseMedia<Tbase_set>::HasSet(const ContentInfo *ci, bool md5sum)
{
	return false;
}

#endif /* ENABLE_NETWORK */

/**
 * Count the number of available graphics sets.
 * @return the number of sets
 */
template <class Tbase_set>
/* static */ int BaseMedia<Tbase_set>::GetNumSets()
{
	int n = 0;
	for (const Tbase_set *s = BaseMedia<Tbase_set>::available_sets; s != NULL; s = s->next) {
		if (s != BaseMedia<Tbase_set>::used_set && s->GetNumMissing() != 0) continue;
		n++;
	}
	return n;
}

/**
 * Get the index of the currently active graphics set
 * @return the current set's index
 */
template <class Tbase_set>
/* static */ int BaseMedia<Tbase_set>::GetIndexOfUsedSet()
{
	int n = 0;
	for (const Tbase_set *s = BaseMedia<Tbase_set>::available_sets; s != NULL; s = s->next) {
		if (s == BaseMedia<Tbase_set>::used_set) return n;
		if (s->GetNumMissing() != 0) continue;
		n++;
	}
	return -1;
}

/**
 * Get the name of the graphics set at the specified index
 * @return the name of the set
 */
template <class Tbase_set>
/* static */ const Tbase_set *BaseMedia<Tbase_set>::GetSet(int index)
{
	for (const Tbase_set *s = BaseMedia<Tbase_set>::available_sets; s != NULL; s = s->next) {
		if (s != BaseMedia<Tbase_set>::used_set && s->GetNumMissing() != 0) continue;
		if (index == 0) return s;
		index--;
	}
	error("Base%s::GetSet(): index %d out of range", Tbase_set::set_type, index);
}

/**
 * Return the used set.
 * @return the used set.
 */
template <class Tbase_set>
/* static */ const Tbase_set *BaseMedia<Tbase_set>::GetUsedSet()
{
	return BaseMedia<Tbase_set>::used_set;
}

/**
 * Return the available sets.
 * @return The available sets.
 */
template <class Tbase_set>
/* static */ Tbase_set *BaseMedia<Tbase_set>::GetAvailableSets()
{
	return BaseMedia<Tbase_set>::available_sets;
}

/**
 * Force instantiation of methods so we don't get linker errors.
 * @param repl_type the type of the BaseMedia to instantiate
 * @param set_type  the type of the BaseSet to instantiate
 */
#define INSTANTIATE_BASE_MEDIA_METHODS(repl_type, set_type) \
	template const char *repl_type::ini_set; \
	template const char *repl_type::GetExtension(); \
	template bool repl_type::AddFile(const char *filename, size_t pathlength, const char *tar_filename); \
	template bool repl_type::HasSet(const struct ContentInfo *ci, bool md5sum); \
	template bool repl_type::SetSet(const char *name); \
	template void repl_type::GetSetsList (stringb *buf); \
	template int repl_type::GetNumSets(); \
	template int repl_type::GetIndexOfUsedSet(); \
	template const set_type *repl_type::GetSet(int index); \
	template const set_type *repl_type::GetUsedSet(); \
	template bool repl_type::DetermineBestSet(); \
	template set_type *repl_type::GetAvailableSets(); \
	template const char *TryGetBaseSetFile(const ContentInfo *ci, bool md5sum, const set_type *s);

