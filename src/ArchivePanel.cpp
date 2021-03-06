
/*******************************************************************
 * SLADE - It's a Doom Editor
 * Copyright (C) 2008-2014 Simon Judd
 *
 * Email:       sirjuddington@gmail.com
 * Web:         http://slade.mancubus.net
 * Filename:    ArchivePanel.cpp
 * Description: ArchivePanel class. The base wxWidgets panel for
 *              archive content editing. One of these is opened in
 *              a tab for each open archive.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *******************************************************************/


/*******************************************************************
 * INCLUDES
 *******************************************************************/
#include "Main.h"
#include "Misc.h"
#include "WxStuff.h"
#include "ArchivePanel.h"
#include "WadArchive.h"
#include "TextEntryPanel.h"
#include "DefaultEntryPanel.h"
#include "GfxEntryPanel.h"
#include "PaletteEntryPanel.h"
#include "AnimatedEntryPanel.h"
#include "SwitchesEntryPanel.h"
#include "HexEntryPanel.h"
#include "ANSIEntryPanel.h"
#include "MapEntryPanel.h"
#include "AudioEntryPanel.h"
#include "GfxConvDialog.h"
#include "ModifyOffsetsDialog.h"
#include "EntryOperations.h"
#include "Clipboard.h"
#include "ArchiveManager.h"
#include "MainWindow.h"
#include "SplashWindow.h"
#include "ArchiveOperations.h"
#include "Icons.h"
#include "Conversions.h"
#include "MainWindow.h"
#include "TranslationEditorDialog.h"
#include "SFileDialog.h"
#include "MapEditorWindow.h"
#include "KeyBind.h"
#include "MapEditorConfigDialog.h"
#include "PaletteManager.h"
#include "MapReplaceDialog.h"
#include "Dialogs/RunDialog.h"
#include <wx/aui/auibook.h>
#include <wx/aui/auibar.h>
#include <wx/filename.h>
#include <wx/gbsizer.h>

/*******************************************************************
 * VARIABLES
 *******************************************************************/
CVAR(Int, autosave_entry_changes, 2, CVAR_SAVE)	// 0=no, 1=yes, 2=ask
CVAR(Bool, confirm_entry_delete, true, CVAR_SAVE)
CVAR(Bool, context_submenus, true, CVAR_SAVE)
CVAR(String, last_colour, "RGB(255, 0, 0)", CVAR_SAVE)
CVAR(String, last_tint_colour, "RGB(255, 0, 0)", CVAR_SAVE)
CVAR(Int, last_tint_amount, 50, CVAR_SAVE)
CVAR(Bool, auto_entry_replace, false, CVAR_SAVE)
EXTERN_CVAR(String, path_pngout);
EXTERN_CVAR(String, path_pngcrush);
EXTERN_CVAR(String, path_deflopt);
wxMenu* menu_archive = NULL;
wxMenu* menu_entry = NULL;
wxAuiToolBar* tb_archive = NULL;
wxAuiToolBar* tb_entry = NULL;


/*******************************************************************
 * APENTRYLISTDROPTARGET CLASS
 *******************************************************************
 Handles drag'n'drop of files on to the entry list
*/
class APEntryListDropTarget : public wxFileDropTarget
{
private:
	ArchivePanel* parent;
	ArchiveEntryList* list;

public:
	APEntryListDropTarget(ArchivePanel* parent, ArchiveEntryList* list) { this->parent = parent; this->list = list; }
	~APEntryListDropTarget() {}

	bool OnDropFiles(wxCoord x, wxCoord y, const wxArrayString& filenames)
	{
		// Determine what item the files were dragged onto
		int flags;
		long index = list->HitTest(wxPoint(x, y), flags) - list->entriesBegin();

		// Add to end if no item was hit
		if (index < 0)
			index = list->GetItemCount() - list->entriesBegin();

		bool yes_to_all = false;
		string caption = (filenames.size() > 1) ? "Overwrite entries" : "Overwrite entry";

		// Import all dragged files, inserting after the item they were dragged onto
		for (int a = filenames.size()-1; a >= 0; a--)
		{
			// Is this a directory?
			if (wxDirExists(filenames[a]))
			{
				// TODO: Handle folders with recursively importing all content
				// and converting to namespaces if dropping in a treeless archive.
			}
			else
			{
				wxFileName fn(filenames[a]);
				ArchiveEntry* entry = NULL;

				// Find entry to replace if needed
				if (auto_entry_replace)
				{
					entry = parent->getArchive()->entryAtPath(list->getCurrentDir()->getPath() + fn.GetFullName());
					// An entry with that name is already present, so ask about replacing it
					if (entry && !yes_to_all)
					{
						// Since there is no standard "Yes/No to all" button or "Don't ask me again" checkbox,
						// we will instead hack the Cancel button into being a "Yes to all" button. This is
						// despite the existence of a wxID_YESTOALL return value...
						string message = S_FMT("Overwrite existing entry %s%s", list->getCurrentDir()->getPath(), fn.GetFullName());
						wxMessageDialog dlg(parent, message, caption, wxCANCEL|wxYES_NO|wxCENTRE);
						dlg.SetYesNoCancelLabels(_("Yes"), _("No"), _("Yes to all"));
						int result = dlg.ShowModal();

						// User doesn't want to replace the entry
						if (result == wxID_NO)			entry = NULL;
						// User wants to replace all entries
						if (result == wxID_CANCEL)		yes_to_all = true;
					}
				}

				// Create new entry if needed
				if (entry == NULL)
					entry = parent->getArchive()->addNewEntry(fn.GetFullName(), index, list->getCurrentDir());

				// Import the file to it
				entry->importFile(filenames[a]);
				EntryType::detectEntryType(entry);
			}
		}

		return true;
	}
};

/*******************************************************************
 * CHOOSEPALETTEDIALOG CLASS
 *******************************************************************
 A simple dialog for the 'Choose Palette' function when creating a
 new palette entry.
 */
class ChoosePaletteDialog : public wxDialog
{
private:
	PaletteChooser*	pal_chooser;

public:
	ChoosePaletteDialog(wxWindow* parent)
		: wxDialog(parent, -1, "Choose Base Palette", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE|wxRESIZE_BORDER)
	{

		// Set dialog icon
		wxIcon icon;
		icon.CopyFromBitmap(getIcon("e_palette"));
		SetIcon(icon);

		// Setup main sizer
		wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
		SetSizer(sizer);

		// Add choose
		pal_chooser = new PaletteChooser(this, -1);
		sizer->Add(pal_chooser, 0, wxEXPAND|wxALL, 4);

		sizer->Add(CreateButtonSizer(wxOK|wxCANCEL), 0, wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM, 4);

		// Init layout
		Layout();

		// Setup dialog size
		SetInitialSize(wxSize(-1, -1));
		SetMinSize(GetSize());
	}

	int getChoice()
	{
		return pal_chooser->GetSelection();
	}
};

/*******************************************************************
 * ARCHIVEPANEL ANCILLARY FUNCTIONS
 *******************************************************************
 Used by the entry sort function.
 */

/* initNamespaceVector
 * Creates a vector of namespaces in a predefined order
 */
void initNamespaceVector(vector<string> &ns, bool flathack)
{
	ns.clear();
	if (flathack)
		ns.push_back("flats");
	ns.push_back("global");
	ns.push_back("colormaps");
	ns.push_back("acs");
	ns.push_back("maps");
	ns.push_back("sounds");
	ns.push_back("music");
	ns.push_back("voices");
	ns.push_back("voxels");
	ns.push_back("graphics");
	ns.push_back("sprites");
	ns.push_back("patches");
	ns.push_back("textures");
	ns.push_back("hires");
	if (!flathack)
		ns.push_back("flats");
}

/* isInMap
 * Checks through a mapdesc_t vector and returns which one, 
 * if any, the entry index is in, -1 otherwise
 */
int isInMap(size_t index, vector<Archive::mapdesc_t> &maps)
{
	for (size_t m = 0; m < maps.size(); ++m)
	{
		size_t head_index = maps[m].head->getParentDir()->entryIndex(maps[m].head);
		size_t end_index = maps[m].head->getParentDir()->entryIndex(maps[m].end, head_index);
		if (index >= head_index && index <= end_index)
			return m;
	}
	return -1;
}

/* getNamespaceNumber
 * Returns the position of the given entry's detected 
 * namespace in the namespace vector. Also hacks around
 * a bit to put less entries in the global namespace
 * and allow sorting a bit by categories.
 */
size_t getNamespaceNumber(ArchiveEntry * entry, size_t index, vector<string> &ns, vector<Archive::mapdesc_t> &maps)
{
	string ens = entry->getParent()->detectNamespace(index);
	if (S_CMPNOCASE(ens, "global"))
	{
		if (maps.size() > 0 && isInMap(index, maps) >= 0)
			ens = "maps";
		else if (S_CMPNOCASE(entry->getType()->getCategory(), "Graphics"))
			ens = "graphics";
		else if (S_CMPNOCASE(entry->getType()->getCategory(), "Audio"))
		{
			if (S_CMPNOCASE(entry->getType()->getIcon(), "e_music"))
				ens = "music";
			else ens = "sounds";
		}
	}
	for (size_t n = 0; n < ns.size(); ++n)
		if (S_CMPNOCASE(ns[n], ens))
			return n;

	ns.push_back(ens);
	return ns.size();
}


/*******************************************************************
 * ARCHIVEPANEL CLASS FUNCTIONS
 *******************************************************************/

/* ArchivePanel::ArchivePanel
 * ArchivePanel class constructor
 *******************************************************************/
ArchivePanel::ArchivePanel(wxWindow* parent, Archive* archive)
	: wxPanel(parent, -1)
{
	// Init variables
	undo_manager = new UndoManager();

	// Set archive
	this->archive = archive;
	listenTo(archive);

	// Create entry panels
	entry_area = new EntryPanel(this, "nil");
	default_area = new DefaultEntryPanel(this);
	text_area = new TextEntryPanel(this);
	gfx_area = new GfxEntryPanel(this);
	pal_area = new PaletteEntryPanel(this);
	animated_area = new AnimatedEntryPanel(this);
	switches_area = new SwitchesEntryPanel(this);
	hex_area = new HexEntryPanel(this);
	ansi_area = new ANSIEntryPanel(this);
	map_area = new MapEntryPanel(this);
	audio_area = new AudioEntryPanel(this);


	// --- Setup Layout ---

	// Create sizer
	wxBoxSizer* m_hbox = new wxBoxSizer(wxHORIZONTAL);
	SetSizer(m_hbox);

	// Entry list panel

	// Create & set sizer & border
	wxStaticBox* frame = new wxStaticBox(this, -1, "Entries");
	wxStaticBoxSizer* framesizer = new wxStaticBoxSizer(frame, wxVERTICAL);
	m_hbox->Add(framesizer, 0, wxEXPAND|wxALL, 4);


	// Create path display
	sizer_path_controls = new wxBoxSizer(wxHORIZONTAL);
	framesizer->Add(sizer_path_controls, 0, wxEXPAND|wxLEFT|wxRIGHT|wxTOP, 4);
	framesizer->AddSpacer(2);

	// Label
	label_path = new wxStaticText(this, -1, "Path:", wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_START);
	sizer_path_controls->Add(label_path, 1, wxRIGHT|wxALIGN_CENTER_VERTICAL, 4);

	// 'Up' button
	btn_updir = new wxBitmapButton(this, -1, getIcon("e_upfolder"));
	btn_updir->Enable(false);
	sizer_path_controls->Add(btn_updir, 0, wxEXPAND);


	// Create entry list panel
	entry_list = new ArchiveEntryList(this);
	entry_list->setArchive(archive);
	entry_list->SetDropTarget(new APEntryListDropTarget(this, entry_list));
	entry_list->setUndoManager(undo_manager);
	framesizer->Add(entry_list, 1, wxEXPAND | wxLEFT|wxRIGHT|wxBOTTOM, 4);


	wxGridBagSizer* gb_sizer = new wxGridBagSizer(4, 4);
	framesizer->Add(gb_sizer, 0, wxEXPAND|wxALL, 4);

	// Create category selector
	choice_category = new wxChoice(this, -1);
	choice_category->Append("All");
	vector<string> cats = EntryType::allCategories();
	for (unsigned a = 0; a < cats.size(); a++)
		choice_category->Append(cats[a]);
	choice_category->SetSelection(0);
	gb_sizer->Add(new wxStaticText(this, -1, "Show:"), wxGBPosition(0, 0), wxDefaultSpan, wxALIGN_CENTER_VERTICAL);
	gb_sizer->Add(choice_category, wxGBPosition(0, 1), wxDefaultSpan, wxEXPAND);
	gb_sizer->AddGrowableCol(1, 1);

	// Create filter
	text_filter = new wxTextCtrl(this, -1);
	gb_sizer->Add(new wxStaticText(this, -1, "Filter:"), wxGBPosition(1, 0), wxDefaultSpan, wxALIGN_CENTER_VERTICAL);
	gb_sizer->Add(text_filter, wxGBPosition(1, 1), wxDefaultSpan, wxEXPAND);


	// Add default entry panel
	cur_area = entry_area;
	m_hbox->Add(cur_area, 1, wxEXPAND);
	cur_area->Show(true);
	cur_area->setUndoManager(undo_manager);

	// Bind events
	entry_list->Bind(EVT_VLV_SELECTION_CHANGED, &ArchivePanel::onEntryListSelectionChange, this);
	entry_list->Bind(wxEVT_LIST_ITEM_FOCUSED, &ArchivePanel::onEntryListFocusChange, this);
	entry_list->Bind(wxEVT_KEY_DOWN, &ArchivePanel::onEntryListKeyDown, this);
	entry_list->Bind(wxEVT_LIST_ITEM_RIGHT_CLICK, &ArchivePanel::onEntryListRightClick, this);
	entry_list->Bind(wxEVT_LIST_ITEM_ACTIVATED, &ArchivePanel::onEntryListActivated, this);
	text_filter->Bind(wxEVT_TEXT, &ArchivePanel::onTextFilterChanged, this);
	choice_category->Bind(wxEVT_CHOICE, &ArchivePanel::onChoiceCategoryChanged, this);
	Bind(EVT_AEL_DIR_CHANGED, &ArchivePanel::onDirChanged, this);
	btn_updir->Bind(wxEVT_BUTTON, &ArchivePanel::onBtnUpDir, this);
	//((DefaultEntryPanel*)default_area)->getEditTextButton()->Bind(wxEVT_BUTTON, &ArchivePanel::onDEPEditAsText, this);
	//((DefaultEntryPanel*)default_area)->getViewHexButton()->Bind(wxEVT_BUTTON, &ArchivePanel::onDEPViewAsHex, this);

	// Do a quick check to see if we need the path display
	if (archive->getRoot()->nChildren() == 0)
		sizer_path_controls->Show(false);

	// Update size+layout
	entry_list->updateWidth();
	Layout();
}

/* ArchivePanel::~ArchivePanel
 * ArchivePanel class destructor
 *******************************************************************/
ArchivePanel::~ArchivePanel()
{
	delete undo_manager;
}

/* ArchivePanel::saveEntryChanges
 * Saves any changes made to the currently open entry
 *******************************************************************/
bool ArchivePanel::saveEntryChanges()
{
	// Ignore if no changes have been made (or no entry is open)
	if (!cur_area->isModified() || !cur_area->getEntry())
		return true;

	// Don't save if autosave is off
	if (autosave_entry_changes == 0)
		return false;

	// Ask if needed
	if (autosave_entry_changes > 1)
	{
		int result = wxMessageBox(S_FMT("Save changes to entry \"%s\"?", cur_area->getEntry()->getName()),
		                          "Unsaved Changes", wxYES_NO|wxICON_QUESTION);

		// Stop if user clicked no
		if (result == wxNO)
			return false;
	}

	// Save entry changes
	return cur_area->saveEntry();
}

/* ArchivePanel::addMenus
 * Adds the 'Archive' and 'Entry' menus to the main window menubar
 *******************************************************************/
void ArchivePanel::addMenus()
{
	// Create menus if needed
	if (!menu_archive)
	{
		// Archive menu
		wxMenu* menu_new = new wxMenu("");
		theApp->getAction("arch_newentry")->addToMenu(menu_new, "&Entry");
		theApp->getAction("arch_newdir")->addToMenu(menu_new, "&Directory");
		theApp->getAction("arch_newpalette")->addToMenu(menu_new, "&PLAYPAL");
		theApp->getAction("arch_newanimated")->addToMenu(menu_new, "&ANIMATED");
		theApp->getAction("arch_newswitches")->addToMenu(menu_new, "&SWITCHES");
		menu_archive = new wxMenu();
		menu_archive->AppendSubMenu(menu_new, "&New");
		theApp->getAction("arch_importfiles")->addToMenu(menu_archive);
		menu_archive->AppendSeparator();
		theApp->getAction("arch_texeditor")->addToMenu(menu_archive);
		theApp->getAction("arch_mapeditor")->addToMenu(menu_archive);
		wxMenu* menu_clean = new wxMenu("");
		theApp->getAction("arch_clean_patches")->addToMenu(menu_clean);
		theApp->getAction("arch_clean_textures")->addToMenu(menu_clean);
		theApp->getAction("arch_clean_flats")->addToMenu(menu_clean);
		theApp->getAction("arch_clean_iwaddupes")->addToMenu(menu_clean);
		theApp->getAction("arch_check_duplicates")->addToMenu(menu_clean);
		theApp->getAction("arch_check_duplicates2")->addToMenu(menu_clean);
		theApp->getAction("arch_replace_maps")->addToMenu(menu_clean);
		menu_archive->AppendSubMenu(menu_clean, "&Maintenance");
	}
	if (!menu_entry)
	{
		// Entry menu
		menu_entry = new wxMenu();
		theApp->getAction("arch_entry_rename")->addToMenu(menu_entry);
		theApp->getAction("arch_entry_delete")->addToMenu(menu_entry);
		theApp->getAction("arch_entry_revert")->addToMenu(menu_entry);
		menu_entry->AppendSeparator();
		theApp->getAction("arch_entry_cut")->addToMenu(menu_entry);
		theApp->getAction("arch_entry_copy")->addToMenu(menu_entry);
		theApp->getAction("arch_entry_paste")->addToMenu(menu_entry);
		menu_entry->AppendSeparator();
		theApp->getAction("arch_entry_moveup")->addToMenu(menu_entry);
		theApp->getAction("arch_entry_movedown")->addToMenu(menu_entry);
		theApp->getAction("arch_entry_sort")->addToMenu(menu_entry);
		menu_entry->AppendSeparator();
		theApp->getAction("arch_entry_import")->addToMenu(menu_entry);
		theApp->getAction("arch_entry_export")->addToMenu(menu_entry);
		menu_entry->AppendSeparator();
		theApp->getAction("arch_entry_bookmark")->addToMenu(menu_entry);
	}

	// Add them to the main window menubar
	theMainWindow->addCustomMenu(menu_archive, "&Archive");
	theMainWindow->addCustomMenu(menu_entry, "&Entry");
	cur_area->addCustomMenu();
	cur_area->addCustomToolBar();

	// Also enable the related toolbars
	theMainWindow->enableToolBar("_archive");
	theMainWindow->enableToolBar("_entry");
}

/* ArchivePanel::removeMenus
 * Removes the 'Archive' and 'Entry' menus from the main window
 * menubar
 *******************************************************************/
void ArchivePanel::removeMenus()
{
	// Remove ArchivePanel menus from the main window menubar
	theMainWindow->removeCustomMenu(menu_archive);
	theMainWindow->removeCustomMenu(menu_entry);
	cur_area->removeCustomMenu();
	cur_area->removeCustomToolBar();

	// Also disable the related toolbars
	theMainWindow->enableToolBar("_archive", false);
	theMainWindow->enableToolBar("_entry", false);
}

/* ArchivePanel::undo
 * Performs an undo operation
 *******************************************************************/
void ArchivePanel::undo()
{
	if (!(cur_area && cur_area->undo()))
	{
		// Undo
		undo_manager->undo();

		// Refresh entry list
		entry_list->updateList();
	}
}

/* ArchivePanel::redo
 * Performs a redo operation
 *******************************************************************/
void ArchivePanel::redo()
{
	if (!(cur_area && cur_area->redo()))
	{
		// Redo
		undo_manager->redo();

		// Refresh entry list
		entry_list->updateList();
	}
}

/* ArchivePanel::save
 * Saves the archive
 *******************************************************************/
bool ArchivePanel::save()
{
	// Check the archive exists
	if (!archive)
		return false;

	// Save any changes in the current entry panel
	saveEntryChanges();

	// Check the archive has been previously saved
	if (!archive->canSave())
		return saveAs();

	// Save the archive
	if (!archive->save())
	{
		// If there was an error pop up a message box
		wxMessageBox(S_FMT("Error:\n%s", Global::error), "Error", wxICON_ERROR);
		return false;
	}

	// Refresh entry list
	entry_list->updateList();

	return true;
}

/* ArchivePanel::saveAs
 * Saves the archive to a new file
 *******************************************************************/
bool ArchivePanel::saveAs()
{
	// Check the archive exists
	if (!archive)
		return false;

	// Do save dialog
	SFileDialog::fd_info_t info;
	if (SFileDialog::saveFile(info, "Save Archive " + archive->getFilename(false) + " As", archive->getFileExtensionString(), this))
	{
		// Save the archive
		if (!archive->save(info.filenames[0]))
		{
			// If there was an error pop up a message box
			wxMessageBox(S_FMT("Error:\n%s", Global::error), "Error", wxICON_ERROR);
			return false;
		}
	}

	// Refresh entry list
	entry_list->updateList();

	// Add as recent file
	theArchiveManager->addRecentFile(info.filenames[0]);

	return true;
}

/* ArchivePanel::newEntry
 * Adds a new entry to the archive after the last selected entry in
 * the list. If nothing is selected it is added at the end of the
 * list. Asks the user for a name for the new entry, and doesn't add
 * one if no name is entered. Returns true if the entry was created,
 * false otherwise.
 *******************************************************************/
bool ArchivePanel::newEntry(int type)
{
	// Prompt for new entry name if needed
	string name;
	switch (type)
	{
	default:
	case ENTRY_EMPTY:
		name = wxGetTextFromUser("Enter new entry name:", "New Entry");
		break;
	case ENTRY_PALETTE:
		name = "playpal.lmp";
		break;
	case ENTRY_ANIMATED:
		name = "animated.lmp";
		break;
	case ENTRY_SWITCHES:
		name = "switches.lmp";
		break;
	}

	// Check if any name was entered
	if (name == "")
		return false;

	// Check for \ character (e.g., from Arch-Viles graphics). They have to be kept.
	if (archive->getType() == ARCHIVE_WAD && name.length() <= 8
	        && (name.find('\\') != wxNOT_FOUND) || (name.find('/') != wxNOT_FOUND))
	{
	} // Don't process as a file name

	// Remove any path from the name, if any (for now)
	else
	{
		wxFileName fn(name);
		name = fn.GetFullName();
	}

	// Get the entry index of the last selected list item
	int index = archive->entryIndex(entry_list->getLastSelectedEntry(), entry_list->getCurrentDir());

	// If something was selected, add 1 to the index so we add the new entry after the last selected
	if (index >= 0)
		index++;
	else
		index = -1;	// If not add to the end of the list

	// Add the entry to the archive
	undo_manager->beginRecord("Add Entry");
	ArchiveEntry* new_entry = archive->addNewEntry(name, index, entry_list->getCurrentDir());
	undo_manager->endRecord(true);

	// Deal with specific entry type that we may want created
	if (type && new_entry)
	{
		ArchiveEntry* e_import;
		MemChunk mc;
		ChoosePaletteDialog cp(this);
		switch (type)
		{
			// Import a palette from the available ones
		case ENTRY_PALETTE:
			if (cp.ShowModal() == wxID_OK)
			{
				Palette8bit* pal;
				int choice = cp.getChoice();
				if (choice)
					pal = thePaletteManager->getPalette(choice - 1);
				else pal = thePaletteManager->globalPalette();
				pal->saveMem(mc);
			}
			else
			{
				mc.reSize(256 * 3);
			}
			new_entry->importMemChunk(mc);
			break;
			// Import the ZDoom definitions as a baseline
		case ENTRY_ANIMATED:
			e_import = theArchiveManager->programResourceArchive()->entryAtPath("animated.lmp");
			if (e_import)
				new_entry->importEntry(e_import);
			break;
			// Import the Boom definitions as a baseline
		case ENTRY_SWITCHES:
			e_import = theArchiveManager->programResourceArchive()->entryAtPath("switches.lmp");
			if (e_import)
				new_entry->importEntry(e_import);
			break;
			// This is just to silence compilers that insist on default cases being handled
		default:
			break;
		}
	}

	// Return whether the entry was created ok
	return !!new_entry;
}

/* ArchivePanel::newDirectory
 * Adds a new subdirectory to the current directory, but only if the
 * archive supports them
 *******************************************************************/
bool ArchivePanel::newDirectory()
{
	// Check archive supports directories
	if (archive->getType() != ARCHIVE_ZIP && archive->getType() != ARCHIVE_FOLDER)
	{
		wxMessageBox("This Archive format does not support directories", "Can't create new directory", wxICON_ERROR);
		return false;
	}

	// Prompt for new directory name
	string name = wxGetTextFromUser("Enter new directory name:", "New Directory");

	// Check if any name was entered
	if (name == "")
		return false;

	// Remove any path from the name, if any (for now)
	wxFileName fn(name);
	name = fn.GetFullName();

	// Add the directory to the archive
	undo_manager->beginRecord("Create Directory");
	ArchiveTreeNode* dir = archive->createDir(name, entry_list->getCurrentDir());
	undo_manager->endRecord(!!dir);

	// Return whether the directory was created ok
	return !!dir;
}

/* ArchivePanel::importFiles
 * Opens a file selection dialog and imports any selected files to
 * the current directory, using the filenames as entry names
 *******************************************************************/
bool ArchivePanel::importFiles()
{
	// Run open files dialog
	SFileDialog::fd_info_t info;
	if (SFileDialog::openFiles(info, "Choose files to import", "Any File (*.*)|*.*", this))
	{
		// Get the entry index of the last selected list item
		int index = archive->entryIndex(entry_list->getLastSelectedEntry(), entry_list->getCurrentDir());

		// If something was selected, add 1 to the index so we add the new entry after the last selected
		if (index >= 0)
			index++;
		else
			index = -1;	// If not add to the end of the list

		// Begin recording undo level
		undo_manager->beginRecord("Import Files");

		// Go through the list of files
		bool ok = false;
		entry_list->Show(false);
		theSplashWindow->show("Importing Files...", true);
		for (size_t a = 0; a < info.filenames.size(); a++)
		{
			// Get filename
			string name = wxFileName(info.filenames[a]).GetFullName();

			// Update splash window
			theSplashWindow->setProgress(float(a) / float(info.filenames.size()));
			theSplashWindow->setProgressMessage(name);

			// Add the entry to the archive
			ArchiveEntry* new_entry = archive->addNewEntry(name, index, entry_list->getCurrentDir());

			// If the entry was created ok, load the file into it
			if (new_entry)
			{
				new_entry->importFile(info.filenames[a]);		// Import file to entry
				EntryType::detectEntryType(new_entry);	// Detect entry type
				ok = true;
			}

			if (index > 0) index++;
		}
		theSplashWindow->hide();
		entry_list->Show(true);

		// End recording undo level
		undo_manager->endRecord(true);

		return ok;
	}
	else	// User cancelled, return false
		return false;
}

/* ArchivePanel::convertArchiveTo
 * Not implemented
 *******************************************************************/
bool ArchivePanel::convertArchiveTo()
{
	wxMessageBox("Not Implemented");
	return false;
}

/* ArchivePanel::cleanupArchive
 * Not implemented
 *******************************************************************/
bool ArchivePanel::cleanupArchive()
{
	wxMessageBox("Not Implemented");
	return false;
}

/* ArchivePanel::renameEntry
 * Opens a dialog to rename the selected entries. If multiple entries
 * are selected, a mass-rename is performed
 *******************************************************************/
bool ArchivePanel::renameEntry(bool each)
{
	// Get a list of selected entries
	vector<ArchiveEntry*> selection = entry_list->getSelectedEntries();

	// Begin recording undo level
	undo_manager->beginRecord("Rename Entry");

	// Check any are selected
	if (each || selection.size() == 1)
	{
		// If only one entry is selected, or "rename each" mode is desired, just do basic rename
		for (unsigned a = 0; a < selection.size(); a++)
		{

			// Prompt for a new name
			string new_name = wxGetTextFromUser("Enter new entry name: (* = unchanged)", "Rename", selection[a]->getName());

			// Rename entry (if needed)
			if (!new_name.IsEmpty() && selection[a]->getName() != new_name)
				archive->renameEntry(selection[a], new_name);
		}
	}
	else if (selection.size() > 1)
	{
		// Get a list of entry names
		wxArrayString names;
		for (unsigned a = 0; a < selection.size(); a++)
			names.push_back(selection[a]->getName(true));

		// Get filter string
		string filter = Misc::massRenameFilter(names);

		// Prompt for a new name
		string new_name = wxGetTextFromUser("Enter new entry name: (* = unchanged)", "Rename", filter);

		// Apply mass rename to list of names
		if (!new_name.IsEmpty())
		{
			Misc::doMassRename(names, new_name);

			// Go through the list
			for (size_t a = 0; a < selection.size(); a++)
			{
				ArchiveEntry* entry = selection[a];

				// If the entry is a folder then skip it
				if (entry->getType() == EntryType::folderType())
					continue;

				// Get current name as wxFileName for processing
				wxFileName fn(entry->getName());

				// Rename the entry (if needed)
				if (fn.GetName() != names[a])
				{
					fn.SetName(names[a]);							// Change name
					archive->renameEntry(entry, fn.GetFullName());	// Rename in archive
				}
			}
		}
	}


	// Get a list of selected directories
	vector<ArchiveTreeNode*> selected_dirs = entry_list->getSelectedDirectories();

	// Go through the list
	for (size_t a = 0; a < selected_dirs.size(); a++)
	{
		// Get the current directory's name
		string old_name = selected_dirs[a]->getName();

		// Prompt for a new name
		string new_name = wxGetTextFromUser("Enter new directory name:", S_FMT("Rename Directory %s", old_name), old_name);

		// Do nothing if no name was entered
		if (new_name.IsEmpty())
			continue;

		// Discard any given path (for now)
		wxFileName fn(new_name);
		new_name = fn.GetName();

		// Rename the directory if the new entered name is different from the original
		if (new_name != old_name)
			archive->renameDir(selected_dirs[a], new_name);
	}

	// Finish recording undo level
	undo_manager->endRecord(true);

	return true;
}

/* ArchivePanel::deleteEntry
 * Deletes any selected entries from the archive
 *******************************************************************/
bool ArchivePanel::deleteEntry(bool confirm)
{
	// Get a list of selected entries
	vector<ArchiveEntry*> selected_entries = entry_list->getSelectedEntries();

	// Get a list of selected directories
	vector<ArchiveTreeNode*> selected_dirs = entry_list->getSelectedDirectories();

	// Confirmation dialog
	if (confirm_entry_delete && confirm)
	{
		string item;
		int num = selected_entries.size() + selected_dirs.size();
		if (num == 1)
		{
			if (selected_entries.size() == 1)
				item = selected_entries[0]->getName();
			else
				item = selected_dirs[0]->getName();
		}
		else if (num > 0)
			item = S_FMT("these %d items", num);

		if (wxMessageBox(S_FMT("Are you sure you want to delete %s?", item), "Delete Confirmation", wxYES_NO|wxICON_QUESTION) == wxNO)
			return false;
	}

	// Clear the selection
	entry_list->clearSelection();

	// Begin recording undo level
	undo_manager->beginRecord("Delete Entry");

	// Go through the selected entries
	for (int a = selected_entries.size() - 1; a >= 0; a--)
	{
		// Remove from bookmarks
		theArchiveManager->deleteBookmark(selected_entries[a]);

		// Remove the current selected entry if it isn't a directory
		if (selected_entries[a]->getType() != EntryType::folderType())
			archive->removeEntry(selected_entries[a]);
	}

	// Go through the selected directories
	for (int a = selected_dirs.size() - 1; a >= 0; a--)
	{
		// Remove from bookmarks
		theArchiveManager->deleteBookmarksInDir(selected_dirs[a]);

		// Remove the selected directory from the archive
		archive->removeDir(selected_dirs[a]->getName(), entry_list->getCurrentDir());
	}

	// Finish recording undo level
	undo_manager->endRecord(true);

	// Switch to blank entry panel
	wxSizer* sizer = GetSizer();
	cur_area->Show(false);
	cur_area->nullEntry();
	sizer->Replace(cur_area, entry_area);
	cur_area = entry_area;
	cur_area->Show(true);
	Layout();

	return true;
}

/* ArchivePanel::revertEntry
 * Reverts any selected entries
 *******************************************************************/
bool ArchivePanel::revertEntry()
{
	// Get selected entries
	vector<ArchiveEntry*> selected_entries = entry_list->getSelectedEntries();

	// Begin recording undo level
	undo_manager->beginRecord("Revert Entry");

	// Go through selection
	for (unsigned a = 0; a < selected_entries.size(); a++)
	{
		undo_manager->recordUndoStep(new EntryDataUS(selected_entries[a]));
		archive->revertEntry(selected_entries[a]);
	}

	// Finish recording undo level
	undo_manager->endRecord(true);

	// If the entries reverted were the only modified entries in the
	// archive, the archive is no longer modified.
	archive->findModifiedEntries();

	// If there was only one selected entry, chances are its content
	// were displayed, so this should be updated
	if (theActivePanel)
		theActivePanel->callRefresh();

	return true;
}

/* ArchivePanel::moveUp
 * Moves any selected entries up in the list
 *******************************************************************/
bool ArchivePanel::moveUp()
{
	// Get selection
	vector<long> selection = entry_list->getSelection();

	// If nothing is selected, do nothing
	if (selection.size() == 0)
		return false;

	// If the first selected item is at the top of the list
	// or before entries start then don't move anything up
	if (selection[0] <= entry_list->entriesBegin())
		return false;

	// Move each one up by swapping it with the entry above it
	undo_manager->beginRecord("Move Up");
	for (size_t a = 0; a < selection.size(); a++)
		archive->swapEntries(entry_list->getEntryIndex(selection[a]), entry_list->getEntryIndex(selection[a]-1), entry_list->getCurrentDir());
	undo_manager->endRecord(true);

	// Update selection
	entry_list->clearSelection();
	for (unsigned a = 0; a < selection.size(); a++)
		entry_list->selectItem(selection[a] - 1);

	// Ensure top-most entry is visible
	entry_list->EnsureVisible(entry_list->getEntryIndex(selection[0]) - 4);

	// Return success
	return true;
}

/* ArchivePanel::moveDown
 * Moves any selected entries down in the list
 *******************************************************************/
bool ArchivePanel::moveDown()
{
	// Get selection
	vector<long> selection = entry_list->getSelection();

	// If nothing is selected, do nothing
	if (selection.size() == 0)
		return false;

	// If the last selected item is at the end of the list
	// then don't move anything down
	if (selection.back() == entry_list->GetItemCount()-1 || selection.back() < entry_list->entriesBegin())
		return false;

	// Move each one down by swapping it with the entry below it
	undo_manager->beginRecord("Move Down");
	for (int a = selection.size()-1; a >= 0; a--)
		archive->swapEntries(entry_list->getEntryIndex(selection[a]), entry_list->getEntryIndex(selection[a]+1), entry_list->getCurrentDir());
	undo_manager->endRecord(true);

	// Update selection
	entry_list->clearSelection();
	for (unsigned a = 0; a < selection.size(); a++)
		entry_list->selectItem(selection[a] + 1);

	// Ensure bottom-most entry is visible
	entry_list->EnsureVisible(entry_list->getEntryIndex(selection[selection.size() - 1]) + 4);

	// Return success
	return true;
}

/* ArchivePanel::sort
 * Sorts all selected entries. If the selection is empty or only
 * contains one single entry, sort the entire archive instead.
 * Note that a simple sort is not desired for three reasons:
 * 1. Map lumps have to remain in sequence
 * 2. Namespaces should be respected
 * 3. Marker lumps used as separators should also be respected
 * The way we're doing that is more than a bit hacky, sorry.
 * The basic idea is to assign to each entry a sortkey (thanks to
 * ExProps for that) which is prefixed with namespace information.
 * Also, the name of map lumps is replaced by the map name so that
 * they stay together. Finally, the original index is appended so
 * that duplicate names are disambiguated.
 *******************************************************************/
bool ArchivePanel::sort()
{
	// Get selected entries
	vector<long> selection = entry_list->getSelection();
	ArchiveTreeNode* dir = entry_list->getCurrentDir();

	size_t start, stop;

	// Without selection of multiple entries, sort everything instead
	if (selection.size() < 2)
	{
		start = 0;
		stop = dir->numEntries();
	}
	// We need sorting to be contiguous, otherwise it'll destroy maps
	else
	{
		start = selection[0];
		stop = selection[selection.size() - 1] + 1;
	}

	// Make sure everything in the range is selected
	selection.clear();
	selection.resize(stop - start);
	for (size_t i = start; i < stop; ++i)
		selection[i] = i;

	// No sorting needed even after adding everything
	if (selection.size() < 2)
		return false;

	vector<string> nspaces;
	initNamespaceVector(nspaces, dir->getArchive()->hasFlatHack());
	vector<Archive::mapdesc_t> maps = dir->getArchive()->detectMaps();

	string ns = dir->getArchive()->detectNamespace(dir->getEntry(selection[0]));
	size_t nsn = 0, lnsn = 0;

	// Fill a map with <entry name, entry index> pairs
	std::map<string, size_t> emap; emap.clear();
	for (size_t i = 0; i < selection.size(); ++i)
	{
		bool ns_changed = false;
		int mapindex = isInMap(selection[i], maps);
		string mapname;
		ArchiveEntry * entry = dir->getEntry(selection[i]);
		// If this is a map entry, deal with it
		if (maps.size() && mapindex > -1)
		{
			// Keep track of the name
			mapname = maps[mapindex].name;

			// If part of a map is selected, make sure the rest is selected as well
			size_t head_index = maps[mapindex].head->getParentDir()->entryIndex(maps[mapindex].head);
			size_t end_index = maps[mapindex].head->getParentDir()->entryIndex(maps[mapindex].end, head_index);
			// Good thing we can rely on selection being contiguous
			for (size_t a = head_index; a <= end_index; ++a)
			{
				bool selected = (a >= start && a < stop);
				if (!selected) selection.push_back(a);
			}
			if (head_index < start) start = head_index;
			if (end_index+1 > stop) stop = end_index+1;
		}
		else if (dir->getArchive()->detectNamespace(selection[i]) != ns)
		{
			ns = dir->getArchive()->detectNamespace(selection[i]);
			nsn = getNamespaceNumber(entry, selection[i], nspaces, maps) * 1000;
			ns_changed = true;
		}
		else if (mapindex < 0 && (entry->getSize() == 0))
		{
			nsn++;
			ns_changed = true;
		}

		// Local namespace number is not necessarily computed namespace number.
		// This is because the global namespace in wads is bloated and we want more
		// categories than it actually has to offer.
		lnsn = (nsn == 0 ? getNamespaceNumber(entry, selection[i], nspaces, maps)*1000 : nsn);
		string name, ename = entry->getName().Upper();
		// Want to get another hack in this stuff? Yeah, of course you do!
		// This here hack will sort Doom II songs by their associated map.
		if (ename.StartsWith("D_") && S_CMPNOCASE(entry->getType()->getIcon(), "e_music"))
		{
			if		(ename == "D_RUNNIN")	ename = "D_MAP01";
			else if (ename == "D_STALKS")	ename = "D_MAP02";
			else if (ename == "D_COUNTD")	ename = "D_MAP03";
			else if (ename == "D_BETWEE")	ename = "D_MAP04";
			else if (ename == "D_DOOM"  )	ename = "D_MAP05";
			else if (ename == "D_THE_DA")	ename = "D_MAP06";
			else if (ename == "D_SHAWN" )	ename = "D_MAP07";
			else if (ename == "D_DDTBLU")	ename = "D_MAP08";
			else if (ename == "D_IN_CIT")	ename = "D_MAP09";
			else if (ename == "D_DEAD"  )	ename = "D_MAP10";
			else if (ename == "D_STLKS2")	ename = "D_MAP11";
			else if (ename == "D_THEDA2")	ename = "D_MAP12";
			else if (ename == "D_DOOM2" )	ename = "D_MAP13";
			else if (ename == "D_DDTBL2")	ename = "D_MAP14";
			else if (ename == "D_RUNNI2")	ename = "D_MAP15";
			else if (ename == "D_DEAD2" )	ename = "D_MAP16";
			else if (ename == "D_STLKS3")	ename = "D_MAP17";
			else if (ename == "D_ROMERO")	ename = "D_MAP18";
			else if (ename == "D_SHAWN2")	ename = "D_MAP19";
			else if (ename == "D_MESSAG")	ename = "D_MAP20";
			else if (ename == "D_COUNT2")	ename = "D_MAP21";
			else if (ename == "D_DDTBL3")	ename = "D_MAP22";
			else if (ename == "D_AMPIE" )	ename = "D_MAP23";
			else if (ename == "D_THEDA3")	ename = "D_MAP24";
			else if (ename == "D_ADRIAN")	ename = "D_MAP25";
			else if (ename == "D_MESSG2")	ename = "D_MAP26";
			else if (ename == "D_ROMER2")	ename = "D_MAP27";
			else if (ename == "D_TENSE" )	ename = "D_MAP28";
			else if (ename == "D_SHAWN3")	ename = "D_MAP29";
			else if (ename == "D_OPENIN")	ename = "D_MAP30";
			else if (ename == "D_EVIL"  )	ename = "D_MAP31";
			else if (ename == "D_ULTIMA")	ename = "D_MAP32";
			else if (ename == "D_READ_M")	ename = "D_MAP33";
			else if (ename == "D_DM2TTL")	ename = "D_MAP34";
			else if (ename == "D_DM2INT")	ename = "D_MAP35";
		}
		// All map lumps have the same sortkey name so they stay grouped
		if (mapindex > -1)
		{
			name = S_FMT("%08d%-64s%8d", lnsn, mapname, selection[i]);
		}
		// Yet another hack! Make sure namespace start markers are first
		else if (ns_changed)
		{
			name = S_FMT("%08d%-64s%8d", lnsn, wxEmptyString, selection[i]);
		}
		// Generic case: actually use the entry name to sort
		else
		{
			name = S_FMT("%08d%-64s%8d", lnsn, ename, selection[i]);
		}
		// Let the entry remember how it was sorted this time
		entry->exProp("sortkey") = name;
		// Insert sortkey into entry map so it'll be sorted
		emap[name] = selection[i];
	}

	// And now, sort the entries based on the map
	undo_manager->beginRecord("Sort Entries");
	std::map<string, size_t>::iterator itr = emap.begin();
	for (size_t i = start; i < stop; ++i, itr++)
	{
		ArchiveEntry * entry = dir->getEntry(i);
		// If the entry isn't in its sorted place already
		if (i != (size_t)itr->second)
		{
			// Swap the texture in the spot with the sorted one
			dir->swapEntries(i, itr->second);

			// Update the position of the displaced texture in the emap
			string name = entry->exProp("sortkey");
			emap[name] = itr->second;
		}
	}
	undo_manager->endRecord(true);

	// Refresh
	entry_list->updateList();
	archive->setModified(true);

	return true;
}

/* ArchivePanel::bookmark
 * Adds the currently focused archive entry to the list of bookmarks
 *******************************************************************/
bool ArchivePanel::bookmark()
{
	ArchiveEntry* entry = entry_list->getFocusedEntry();

	if (entry)
	{
		theArchiveManager->addBookmark(entry_list->getFocusedEntry());
		return true;
	}
	else
		return false;
}

/* ArchivePanel::openTab
 * Opens currently selected entries in separate tabs
 *******************************************************************/
bool ArchivePanel::openTab()
{
	// Get selected entries
	vector<ArchiveEntry*> selection = entry_list->getSelectedEntries();

	// Open each in its own tab
	for (unsigned a = 0; a < selection.size(); a++)
		theMainWindow->openEntry(selection[a]);

	return true;
}

/* ArchivePanel::crc32
 * Computes the CRC-32 checksums of the selected entries
 *******************************************************************/
bool ArchivePanel::crc32()
{
	// Get selected entries
	vector<ArchiveEntry*> selection = entry_list->getSelectedEntries();

	// Compute CRC-32 checksums for each
	string checksums = "\nCRC-32:\n";
	for (unsigned a = 0; a < selection.size(); a++)
	{
		uint32_t crc = selection[a]->getMCData().crc();
		checksums += S_FMT("%s:\t%x\n", selection[a]->getName(), crc);
	}
	wxLogMessage(checksums);
	wxMessageBox(checksums);

	return true;
}

/* ArchivePanel::convertEntryTo
 * Not implemented
 *******************************************************************/
bool ArchivePanel::convertEntryTo()
{
	wxMessageBox("Not Implemented");
	return false;
}

/* ArchivePanel::importEntry
 * For each selected entry, opens a file selection dialog. The
 * selected file will be imported into the entry
 *******************************************************************/
bool ArchivePanel::importEntry()
{
	// Get a list of selected entries
	vector<ArchiveEntry*> selection = entry_list->getSelectedEntries();

	// Begin recording undo level
	undo_manager->beginRecord("Import Entry");

	// Go through the list
	for (size_t a = 0; a < selection.size(); a++)
	{
		// Run open file dialog
		SFileDialog::fd_info_t info;
		if (SFileDialog::openFile(info, "Import Entry \"" + selection[a]->getName() + "\"", "Any File (*.*)|*.*", this))
		{

			// Preserve gfx offset if needed
			point2_t offset;
			if (!selection[a]->getType()->getEditor().Cmp("gfx"))
			{
				// We have an image
				SImage si;
				si.open(selection[a]->getMCData());
				offset = si.offset();
			}

			// Create undo step
			undo_manager->recordUndoStep(new EntryDataUS(selection[a]));

			// If a file was selected, import it
			selection[a]->importFile(info.filenames[0]);

			// Re-detect entry type
			EntryType::detectEntryType(selection[a]);

			// Restore offsets if needed
			if (!selection[a]->getType()->getEditor().Cmp("gfx"))
			{
				SImage si;
				si.open(selection[a]->getMCData());

				point2_t noffset = si.offset();
				bool ok = true;
				// Don't bother if the same offsets are reimported
				if (offset == noffset) ok = false;
				// Ask for confirmation if there actually are offsets but they are different
				else if (noffset.x | noffset.y)
				{
					wxMessageDialog md(this,
					                   S_FMT("Image %s had offset [%d, %d], imported file has offset [%d, %d]. "
					                         "Do you want to keep the old offset and override the new?",
					                         selection[a]->getName(), offset.x, offset.y, noffset.x, noffset.y),
					                   "Conflicting Offsets", wxYES_NO);
					int result = md.ShowModal();
					if (result != wxID_YES)
						ok = false;
				}
				// Warn if the offsets couldn't be written
				if (ok && !si.getFormat()->writeOffset(si, selection[a], offset))
					wxLogMessage("Old offset information [%d, %d] couldn't be "
					             "preserved in the new image format for image %s.",
					             offset.x, offset.y, selection[a]->getName());
			}

			// Set extension by type
			selection[a]->setExtensionByType();

			// If the entry is currently open, refresh the entry panel
			if (cur_area->getEntry() == selection[a])
				openEntry(selection[a], true);
		}
	}

	// Finish recording undo level
	undo_manager->endRecord(true);

	return true;
}

/* ArchivePanel::exportEntry
 * Exports any selected entries to files. If multiple entries are
 * selected, a directory selection dialog is shown, and any selected
 * entries will be exported to that directory
 *******************************************************************/
bool ArchivePanel::exportEntry()
{
	// Get a list of selected entries
	vector<ArchiveEntry*> selection = entry_list->getSelectedEntries();

	// If we're just exporting 1 entry
	if (selection.size() == 1)
	{
		string name = Misc::lumpNameToFileName(selection[0]->getName());
		wxFileName fn(name);

		// Add appropriate extension if needed
		if (fn.GetExt().Len() == 0) fn.SetExt(selection[0]->getType()->getExtension());

		// Run save file dialog
		SFileDialog::fd_info_t info;
		if (SFileDialog::saveFile(info, "Export Entry \"" + selection[0]->getName() + "\"", "Any File (*.*)|*.*", this, fn.GetFullName()))
			selection[0]->exportFile(info.filenames[0]);	// Export entry if ok was clicked
	}
	else
	{
		// Run save files dialog
		SFileDialog::fd_info_t info;
		if (SFileDialog::saveFiles(info, "Export Multiple Entries (Filename is ignored)", "Any File (*.*)|*.*", this))
		{
			// Go through the selection
			for (size_t a = 0; a < selection.size(); a++)
			{
				// Setup entry filename
				wxFileName fn(selection[a]->getName());
				fn.SetPath(info.path);

				// Add file extension if it doesn't exist
				if (!fn.HasExt())
					fn.SetExt(selection[a]->getType()->getExtension());

				// Do export
				selection[a]->exportFile(fn.GetFullPath());
			}
		}
	}

	return true;
}

/* ArchivePanel::exportEntryAs
 * Not implemented
 *******************************************************************/
bool ArchivePanel::exportEntryAs()
{
	wxMessageBox("Not Implemented");
	return false;
}

/* ArchivePanel::copyEntry
 * Copies selected entries+directories to the clipboard
 *******************************************************************/
bool ArchivePanel::copyEntry()
{
	// Get a list of selected entries
	vector<ArchiveEntry*> entries = entry_list->getSelectedEntries();

	// Get a list of selected directories
	vector<ArchiveTreeNode*> dirs = entry_list->getSelectedDirectories();

	// Do nothing if nothing selected
	if (entries.size() == 0 && dirs.size() == 0)
		return false;

	// Create clipboard item from selection
	theClipboard->addItem(new EntryTreeClipboardItem(entries, dirs));

	return true;
}

/* ArchivePanel::cutEntry
 * Copies selected entries+diretories to the clipboard, and deletes
 * them from the archive
 *******************************************************************/
bool ArchivePanel::cutEntry()
{
	if (copyEntry())
		return deleteEntry(false);
	else
		return false;
}

/* ArchivePanel::pasteEntry
 * Pastes any entries and directories on the clipboard into the
 * current directory. Entries will be pasted after the last selected
 * entry, whereas directories will be pasted after any
 * subdirectories. Pasting a directory will also paste any entries
 * and subdirectories within it.
 *******************************************************************/
bool ArchivePanel::pasteEntry()
{
	// Do nothing if there is nothing in the clipboard
	if (theClipboard->nItems() == 0)
		return false;

	// Get the entry index of the last selected list item
	int index = archive->entryIndex(entry_list->getLastSelectedEntry(), entry_list->getCurrentDir());

	// If something was selected, add 1 to the index so we add the new entry after the last selected
	if (index >= 0)
		index++;
	else
		index = -1;	// If not add to the end of the list

	// Go through all clipboard items
	bool pasted = false;
	undo_manager->beginRecord("Paste Entry");
	for (unsigned a = 0; a < theClipboard->nItems(); a++)
	{
		// Check item type
		if (theClipboard->getItem(a)->getType() != CLIPBOARD_ENTRY_TREE)
			continue;

		// Get clipboard item
		EntryTreeClipboardItem* clip = (EntryTreeClipboardItem*)theClipboard->getItem(a);

		// Merge it in
		if (archive->paste(clip->getTree(), index, entry_list->getCurrentDir()))
			pasted = true;
	}
	undo_manager->endRecord(true);

	if (pasted)
	{
		// Update archive
		archive->setModified(true);

		return true;
	}
	else
		return false;
}

/* ArchivePanel::gfxConvert
 * Opens the Gfx Conversion dialog and sends selected entries to it
 *******************************************************************/
bool ArchivePanel::gfxConvert()
{
	// Create gfx conversion dialog
	GfxConvDialog gcd;

	// Send selection to the gcd
	vector<ArchiveEntry*> selection = entry_list->getSelectedEntries();
	gcd.openEntries(selection);

	// Run the gcd
	gcd.ShowModal();

	// Show splash window
	theSplashWindow->show("Writing converted image data...", true);

	// Begin recording undo level
	undo_manager->beginRecord("Gfx Format Conversion");

	// Write any changes
	for (unsigned a = 0; a < selection.size(); a++)
	{
		// Update splash window
		theSplashWindow->setProgressMessage(selection[a]->getName());
		theSplashWindow->setProgress((float)a / (float)selection.size());

		// Skip if the image wasn't converted
		if (!gcd.itemModified(a))
			continue;

		// Get image and conversion info
		SImage* image = gcd.getItemImage(a);
		SIFormat* format = gcd.getItemFormat(a);

		// Write converted image back to entry
		MemChunk mc;
		format->saveImage(*image, mc, gcd.getItemPalette(a));
		selection[a]->importMemChunk(mc);
		EntryType::detectEntryType(selection[a]);
		selection[a]->setExtensionByType();
	}

	// Finish recording undo level
	undo_manager->endRecord(true);

	// Hide splash window
	theSplashWindow->hide();
	theActivePanel->callRefresh();

	return true;
}

/* ArchivePanel::gfxRemap
 * Opens the Translation editor dialog to remap colours on selected
 * gfx entries
 *******************************************************************/
bool ArchivePanel::gfxRemap()
{
	// Get selected entries
	vector<ArchiveEntry*> selection = entry_list->getSelectedEntries();

	// Create preview image (just use first selected entry)
	SImage image(PALMASK);
	Misc::loadImageFromEntry(&image, selection[0]);

	// Create translation editor dialog
	Palette8bit* pal = theMainWindow->getPaletteChooser()->getSelectedPalette();
	TranslationEditorDialog ted(this, pal, "Colour Remap", &image);
	ted.openTranslation(((GfxEntryPanel*)gfx_area)->prevTranslation());

	// Run dialog
	if (ted.ShowModal() == wxID_OK)
	{
		// Begin recording undo level
		undo_manager->beginRecord("Gfx Colour Remap");

		// Apply translation to all entry images
		SImage temp;
		MemChunk mc;
		for (unsigned a = 0; a < selection.size(); a++)
		{
			ArchiveEntry* entry = selection[a];
			if (Misc::loadImageFromEntry(&temp, entry))
			{
				// Apply translation
				temp.applyTranslation(&ted.getTranslation(), pal);

				// Create undo step
				undo_manager->recordUndoStep(new EntryDataUS(entry));

				// Write modified image data
				if (!temp.getFormat()->saveImage(temp, mc, pal))
					wxLogMessage("Error: Could not write image data to entry %s, unsupported format for writing", entry->getName());
				else
					entry->importMemChunk(mc);
			}
		}

		// Update variables
		((GfxEntryPanel*)gfx_area)->prevTranslation().copy(ted.getTranslation());

		// Finish recording undo level
		undo_manager->endRecord(true);
	}
	theActivePanel->callRefresh();

	return true;
}

/* ArchivePanel::gfxColourise
 * Opens the Colourise dialog to batch-colour selected gfx entries
 *******************************************************************/
bool ArchivePanel::gfxColourise()
{
	// Get selected entries
	vector<ArchiveEntry*> selection = entry_list->getSelectedEntries();

	// Create colourise dialog
	Palette8bit* pal = theMainWindow->getPaletteChooser()->getSelectedPalette();
	GfxColouriseDialog gcd(this, selection[0], pal);
	gcd.setColour(last_colour);

	// Run dialog
	if (gcd.ShowModal() == wxID_OK)
	{
		// Begin recording undo level
		undo_manager->beginRecord("Gfx Colourise");

		// Apply translation to all entry images
		SImage temp;
		MemChunk mc;
		for (unsigned a = 0; a < selection.size(); a++)
		{
			ArchiveEntry* entry = selection[a];
			if (Misc::loadImageFromEntry(&temp, entry))
			{
				// Apply translation
				temp.colourise(gcd.getColour(), pal);

				// Create undo step
				undo_manager->recordUndoStep(new EntryDataUS(entry));

				// Write modified image data
				if (!temp.getFormat()->saveImage(temp, mc, pal))
					wxLogMessage("Error: Could not write image data to entry %s, unsupported format for writing", entry->getName());
				else
					entry->importMemChunk(mc);
			}
		}

		// Finish recording undo level
		undo_manager->endRecord(true);
	}
	rgba_t gcdcol = gcd.getColour();
	last_colour = S_FMT("RGB(%d, %d, %d)", gcdcol.r, gcdcol.g, gcdcol.b);
	theActivePanel->callRefresh();

	return true;
}

/* ArchivePanel::gfxTint
 * Opens the Tint dialog to batch-colour selected gfx entries
 *******************************************************************/
bool ArchivePanel::gfxTint()
{
	// Get selected entries
	vector<ArchiveEntry*> selection = entry_list->getSelectedEntries();

	// Create colourise dialog
	Palette8bit* pal = theMainWindow->getPaletteChooser()->getSelectedPalette();
	GfxTintDialog gtd(this, selection[0], pal);
	gtd.setValues(last_tint_colour, last_tint_amount);

	// Run dialog
	if (gtd.ShowModal() == wxID_OK)
	{
		// Begin recording undo level
		undo_manager->beginRecord("Gfx Tint");

		// Apply translation to all entry images
		SImage temp;
		MemChunk mc;
		for (unsigned a = 0; a < selection.size(); a++)
		{
			ArchiveEntry* entry = selection[a];
			if (Misc::loadImageFromEntry(&temp, entry))
			{
				// Apply translation
				temp.tint(gtd.getColour(), gtd.getAmount(), pal);

				// Create undo step
				undo_manager->recordUndoStep(new EntryDataUS(entry));

				// Write modified image data
				if (!temp.getFormat()->saveImage(temp, mc, pal))
					wxLogMessage("Error: Could not write image data to entry %s, unsupported format for writing", entry->getName());
				else
					entry->importMemChunk(mc);
			}
		}

		// Finish recording undo level
		undo_manager->endRecord(true);
	}
	rgba_t gtdcol = gtd.getColour();
	last_tint_colour = S_FMT("RGB(%d, %d, %d)", gtdcol.r, gtdcol.g, gtdcol.b);
	last_tint_amount = (int)(gtd.getAmount() * 100.0f);
	theActivePanel->callRefresh();

	return true;
}

/* ArchivePanel::gfxModifyOffsets
 * Opens the Modify Offsets dialog to mass-modify offsets of any
 * selected, offset-compatible gfx entries
 *******************************************************************/
bool ArchivePanel::gfxModifyOffsets()
{
	// Create modify offsets dialog
	ModifyOffsetsDialog mod;

	// Run the dialog
	if (mod.ShowModal() == wxID_CANCEL)
		return false;

	// Begin recording undo level
	undo_manager->beginRecord("Gfx Modify Offsets");

	// Go through selected entries
	vector<ArchiveEntry*> selection = entry_list->getSelectedEntries();
	for (uint32_t a = 0; a < selection.size(); a++)
	{
		undo_manager->recordUndoStep(new EntryDataUS(selection[a]));
		EntryOperations::modifyGfxOffsets(selection[a], &mod);
	}
	theActivePanel->callRefresh();

	// Finish recording undo level
	undo_manager->endRecord(true);

	return true;
}

/* ArchivePanel::gfxExportPNG
 * Exports any selected gfx entries as png format images
 *******************************************************************/
bool ArchivePanel::gfxExportPNG()
{
	// Get a list of selected entries
	vector<ArchiveEntry*> selection = entry_list->getSelectedEntries();

	// If we're just exporting 1 entry
	if (selection.size() == 1)
	{
		string name = Misc::lumpNameToFileName(selection[0]->getName());
		wxFileName fn(name);

		// Set extension
		fn.SetExt("png");

		// Run save file dialog
		SFileDialog::fd_info_t info;
		if (SFileDialog::saveFile(info, "Export Entry \"" + selection[0]->getName() + "\" as PNG", "PNG Files (*.png)|*.png", this, fn.GetFullName()))
		{
			// If a filename was selected, export it
			if (!EntryOperations::exportAsPNG(selection[0], info.filenames[0]))
			{
				wxMessageBox(S_FMT("Error: %s", Global::error), "Error", wxOK|wxICON_ERROR);
				return false;
			}
		}

		return true;
	}
	else
	{
		// Run save files dialog
		SFileDialog::fd_info_t info;
		if (SFileDialog::saveFiles(info, "Export Entries as PNG (Filename will be ignored)", "PNG Files (*.png)|*.png", this))
		{
			// Go through the selection
			for (size_t a = 0; a < selection.size(); a++)
			{
				// Setup entry filename
				wxFileName fn(selection[a]->getName());
				fn.SetPath(info.path);
				fn.SetExt("png");

				// Do export
				EntryOperations::exportAsPNG(selection[a], fn.GetFullPath());
			}
		}
	}

	return true;
}

/* ArchivePanel::currentEntry
 * Returns the entry currently open for editing
 *******************************************************************/
ArchiveEntry* ArchivePanel::currentEntry()
{
	if (entry_list->GetSelectedItemCount() == 1)
		return cur_area->getEntry();
	else
		return NULL;
}

/* ArchivePanel::currentEntries
 * Returns a vector of all selected entries
 *******************************************************************/
vector<ArchiveEntry*> ArchivePanel::currentEntries()
{
	vector<ArchiveEntry*> selection;
	if (entry_list)
		selection = entry_list->getSelectedEntries();
	return selection;
}

/* ArchivePanel::currentDir
 * Returns a vector of all selected entries
 *******************************************************************/
ArchiveTreeNode* ArchivePanel::currentDir()
{
	if (entry_list)
		return entry_list->getCurrentDir();
	return NULL;
}

/* ArchivePanel::swanConvert
 * Converts any selected SWANTBLS entries to SWITCHES and ANIMATED
 *******************************************************************/
bool ArchivePanel::swanConvert()
{
	// Get the entry index of the last selected list item
	int index = archive->entryIndex(currentEntry(), entry_list->getCurrentDir());

	// If something was selected, add 1 to the index so we add the new entry after the last selected
	if (index >= 0)
		index++;

	// MemChunks for ANIMATED and SWITCHES
	MemChunk mca, mcs;

	// Get a list of selected entries
	vector<ArchiveEntry*> selection = entry_list->getSelectedEntries();

	bool error = false;

	// Check each selected entry for possible conversion
	for (size_t a = 0; a < selection.size(); a++)
	{
		if (selection[a]->getType()->getId() == "swantbls")
		{
			error |= !AnimatedList::convertSwanTbls(selection[a], &mca);
			error |= !SwitchesList::convertSwanTbls(selection[a], &mcs);
		}
	}

	// Close off ANIMATED lump if needed
	if (mca.getSize())
	{
		uint8_t buffer = 255;
		error |= !mca.reSize(mca.getSize() + 1, true);
		error |= !mca.write(&buffer, 1);
	}
	// Close off SWITCHES lump if needed
	if (mcs.getSize())
	{
		uint8_t buffer[20];
		memset(buffer, 0, 20);
		error |= !mcs.reSize(mcs.getSize() + 20, true);
		error |= !mcs.write(buffer, 20);
	}

	// Create entries
	MemChunk* mc[2] = {&mca, &mcs};
	string wadnames[2] = { "ANIMATED", "SWITCHES" };
	string zipnames[2] = { "animated.lmp", "switches.lmp" };
	string etypeids[2] = { "animated", "switches" };
	for (int e = 0; e < 2; ++e)
	{
		if (mc[e]->getSize())
		{
			// Begin recording undo level
			undo_manager->beginRecord(S_FMT("Creating %s", wadnames[e]));

			ArchiveEntry * output = archive->addNewEntry(
				(archive->getType() == ARCHIVE_WAD ?  wadnames[e] : zipnames[e]), 
				index, entry_list->getCurrentDir());
			if (output)
			{
				error |= !output->importMemChunk(*mc[e]);
				EntryType::detectEntryType(output);
				if (output->getType() == EntryType::unknownType())
					output->setType(EntryType::getType(etypeids[e]));
				if (index >= 0) index++;
			}
			else error = true;
			// Finish recording undo level
			undo_manager->endRecord(true);
		}
	}
	return !error;
}

/* ArchivePanel::basConvert
 * Converts any selected SWITCHES or ANIMATED entries to a newly
 * created ANIMDEFS or SWANTBLS entry
 *******************************************************************/
bool ArchivePanel::basConvert(bool animdefs)
{
	// Get the entry index of the last selected list item
	int index = archive->entryIndex(currentEntry(), entry_list->getCurrentDir());

	// If something was selected, add 1 to the index so we add the new entry after the last selected
	if (index >= 0)
		index++;

	// Get a list of selected entries
	vector<ArchiveEntry*> selection = entry_list->getSelectedEntries();

	// Begin recording undo level
	undo_manager->beginRecord("Convert to ANIMDEFS");

	// Create new entry
	ArchiveEntry* output = archive->addNewEntry(
		(animdefs
		? (archive->getType() == ARCHIVE_WAD ? "ANIMDEFS" : "animdefs.txt")
		: (archive->getType() == ARCHIVE_WAD ? "SWANTBLS" : "swantbls.dat")
		), index, entry_list->getCurrentDir());

	// Finish recording undo level
	undo_manager->endRecord(true);

	// Convert to ZDoom-compatible ANIMDEFS
	if (output)
	{
		// Create the memory buffer
		string gentext; 
		if (animdefs)
		{
			gentext = "// ANIMDEFS lump generated by SLADE3\n// on " + wxNow() + "\n\n";
		}
		else
		{
			gentext = "# SWANTBLS data generated by SLADE 3\n# on " + wxNow() + "\n#\n"
				"# This file is input for SWANTBLS.EXE, it specifies the switchnames\n"
				"# and animated textures and flats usable with BOOM. The output of\n"
				"# SWANTBLS is two lumps, SWITCHES.LMP and ANIMATED.LMP that should\n"
				"# be inserted in the PWAD as lumps.\n#\n";
		}

		MemChunk animdata(gentext.length());
		animdata.seek(0, SEEK_SET);
		animdata.write(CHR(gentext), gentext.length());

		// Check each selected entry for possible conversion
		for (size_t a = 0; a < selection.size(); a++)
		{
			if (selection[a]->getType()->getFormat() == "animated")
				AnimatedList::convertAnimated(selection[a], &animdata, animdefs);
			else if (selection[a]->getType()->getFormat() == "switches")
				SwitchesList::convertSwitches(selection[a], &animdata, animdefs);
		}
		output->importMemChunk(animdata);

		// Identify the new lump as what it is
		EntryType::detectEntryType(output);
		// Failsafe is detectEntryType doesn't accept to work, grumble
		if (output->getType() == EntryType::unknownType())
			output->setType(EntryType::getType("animdefs"));
	}

	// Force entrylist width update
	Layout();

	return (output != NULL);
}

/* ArchivePanel::palConvert
 * Unused (converts 6-bit palette to 8-bit)
 *******************************************************************/
bool ArchivePanel::palConvert()
{
	// Get the entry index of the last selected list item
	ArchiveEntry* pal6bit = currentEntry();
	const uint8_t* source = pal6bit->getData(true);
	uint8_t* dest = new uint8_t[pal6bit->getSize()];
	memcpy(dest, source, pal6bit->getSize());
	for (size_t i = 0; i < pal6bit->getSize(); ++i)
	{
		dest[i] = ((dest[i] << 2) | (dest[i] >> 4));
	}
	pal6bit->importMem(dest, pal6bit->getSize());
	theActivePanel->callRefresh();
	delete[] dest;
	return true;
}

/* ArchivePanel::wavDSndConvert
 * Converts selected wav format entries to doom sound format
 *******************************************************************/
bool ArchivePanel::wavDSndConvert()
{
	// Get selected entries
	vector<ArchiveEntry*> selection = entry_list->getSelectedEntries();

	// Begin recording undo level
	undo_manager->beginRecord("Convert Wav -> Doom Sound");

	// Go through selection
	for (unsigned a = 0; a < selection.size(); a++)
	{
		// Convert WAV -> Doom Sound if the entry is WAV format
		if (selection[a]->getType()->getFormat() == "snd_wav")
		{
			MemChunk dsnd;
			// Attempt conversion
			if (!Conversions::wavToDoomSnd(selection[a]->getMCData(), dsnd))
			{
				wxLogMessage("Error: Unable to convert entry %s: %s", selection[a]->getName(), Global::error);
				continue;
			}
			undo_manager->recordUndoStep(new EntryDataUS(selection[a]));	// Create undo step
			selection[a]->importMemChunk(dsnd);								// Load doom sound data
			EntryType::detectEntryType(selection[a]);						// Update entry type
			selection[a]->setExtensionByType();								// Update extension if necessary
		}
	}

	// Finish recording undo level
	undo_manager->endRecord(true);

	return true;
}

/* ArchivePanel::dSndWavConvert
 * Converts selected doom sound format entries to wav format
 *******************************************************************/
bool ArchivePanel::dSndWavConvert()
{
	// Get selected entries
	vector<ArchiveEntry*> selection = entry_list->getSelectedEntries();

	// Begin recording undo level
	undo_manager->beginRecord("Convert Doom Sound -> Wav");

	// Go through selection
	for (unsigned a = 0; a < selection.size(); a++)
	{
		bool worked = false;
		MemChunk wav;
		// Convert Doom Sound -> WAV if the entry is Doom Sound format
		if (selection[a]->getType()->getFormat() == "snd_doom" ||
		        selection[a]->getType()->getFormat() == "snd_doom_mac")
			worked = Conversions::doomSndToWav(selection[a]->getMCData(), wav);
		// Or Doom Speaker sound format
		else if (selection[a]->getType()->getFormat() == "snd_speaker")
			worked = Conversions::spkSndToWav(selection[a]->getMCData(), wav);
		// Or Jaguar Doom sound format
		else if (selection[a]->getType()->getFormat() == "snd_jaguar")
			worked = Conversions::jagSndToWav(selection[a]->getMCData(), wav);
		// Or Wolfenstein 3D sound format
		else if (selection[a]->getType()->getFormat() == "snd_wolf")
			worked = Conversions::wolfSndToWav(selection[a]->getMCData(), wav);
		// Or Creative Voice File format
		else if (selection[a]->getType()->getFormat() == "snd_voc")
			worked = Conversions::vocToWav(selection[a]->getMCData(), wav);
		// Or Blood SFX format (this one needs to be given the entry, not just the mem chunk)
		else if (selection[a]->getType()->getFormat() == "snd_bloodsfx")
			worked = Conversions::bloodToWav(selection[a], wav);
		// If successfully converted, update the entry
		if (worked)
		{
			undo_manager->recordUndoStep(new EntryDataUS(selection[a]));	// Create undo step
			selection[a]->importMemChunk(wav);								// Load wav data
			EntryType::detectEntryType(selection[a]);						// Update entry type
			selection[a]->setExtensionByType();								// Update extension if necessary
		}
		else
		{
			wxLogMessage("Error: Unable to convert entry %s: %s", selection[a]->getName(), Global::error);
			continue;
		}
	}

	// Finish recording undo level
	undo_manager->endRecord(true);

	return true;
}

/* ArchivePanel::musMidiConvert
 * Converts selected mus format entries to midi format
 *******************************************************************/
bool ArchivePanel::musMidiConvert()
{
	// Get selected entries
	vector<ArchiveEntry*> selection = entry_list->getSelectedEntries();

	// Begin recording undo level
	undo_manager->beginRecord("Convert Mus -> Midi");

	// Go through selection
	for (unsigned a = 0; a < selection.size(); a++)
	{
		// Convert MUS -> MIDI if the entry is a MIDI-like format
		if (selection[a]->getType()->getFormat() == "mus" ||
			selection[a]->getType()->getFormat() == "hmi" ||
			selection[a]->getType()->getFormat() == "hmp" ||
			selection[a]->getType()->getFormat() == "xmi" ||
			selection[a]->getType()->getFormat() == "gmid")
		{
			MemChunk midi;
			undo_manager->recordUndoStep(new EntryDataUS(selection[a]));	// Create undo step
			if (selection[a]->getType()->getFormat() == "mus")
				Conversions::musToMidi(selection[a]->getMCData(), midi);	// Convert
			else if (selection[a]->getType()->getFormat() == "gmid")
				Conversions::gmidToMidi(selection[a]->getMCData(), midi);	// Convert
			else
				Conversions::zmusToMidi(selection[a]->getMCData(), midi);	// Convert
			selection[a]->importMemChunk(midi);								// Load midi data
			EntryType::detectEntryType(selection[a]);						// Update entry type
			selection[a]->setExtensionByType();								// Update extension if necessary
		}

	}

	// Finish recording undo level
	undo_manager->endRecord(true);

	return true;
}

/* ArchivePanel::compileACS
 * Compiles any selected text entries as ACS scripts
 *******************************************************************/
bool ArchivePanel::compileACS(bool hexen)
{
	// Get selected entries
	vector<ArchiveEntry*> selection = entry_list->getSelectedEntries();

	// Go through selection
	for (unsigned a = 0; a < selection.size(); a++)
	{
		// Compile ACS script
		EntryOperations::compileACS(selection[a], hexen, NULL, theMainWindow);
	}

	return true;
}

/* ArchivePanel::optimizePNG
 * Compiles any selected text entries as ACS scripts
 *******************************************************************/
bool ArchivePanel::optimizePNG()
{
	// Check if the PNG tools path are set up, at least one of them should be
	string pngpathc = path_pngcrush;
	string pngpatho = path_pngout;
	string pngpathd = path_deflopt;
	if ((pngpathc.IsEmpty() || !wxFileExists(pngpathc)) &&
	        (pngpatho.IsEmpty() || !wxFileExists(pngpatho)) &&
	        (pngpathd.IsEmpty() || !wxFileExists(pngpathd)))
	{
		wxMessageBox("Error: PNG tool paths not defined or invalid, please configure in SLADE preferences", "Error", wxOK|wxCENTRE|wxICON_ERROR);
		return false;
	}

	// Get selected entries
	vector<ArchiveEntry*> selection = entry_list->getSelectedEntries();

	theSplashWindow->show("Running external programs, please wait...", true);

	// Begin recording undo level
	undo_manager->beginRecord("Optimize PNG");

	// Go through selection
	for (unsigned a = 0; a < selection.size(); a++)
	{
		theSplashWindow->setProgressMessage(selection[a]->getName(true));
		theSplashWindow->setProgress(float(a) / float(selection.size()));
		if (selection[a]->getType()->getFormat() == "img_png")
		{
			undo_manager->recordUndoStep(new EntryDataUS(selection[a]));
			EntryOperations::optimizePNG(selection[a]);
		}
	}
	theSplashWindow->hide();

	// Finish recording undo level
	undo_manager->endRecord(true);

	return true;
}

/* ArchivePanel::convertTextures
 * Converts any selected TEXTUREx entries to a ZDoom TEXTURES entry
 *******************************************************************/
bool ArchivePanel::convertTextures()
{
	// Get selected entries
	long index = entry_list->getSelection()[0];
	vector<ArchiveEntry*> selection = entry_list->getSelectedEntries();

	// Begin recording undo level
	undo_manager->beginRecord("Convert TEXTUREx -> TEXTURES");

	// Do conversion
	if (EntryOperations::convertTextures(selection))
	{
		// Select new TEXTURES entry
		entry_list->clearSelection();
		entry_list->selectItem(index);

		// Finish recording undo level
		undo_manager->endRecord(true);

		return true;
	}

	// Finish recording undo level
	undo_manager->endRecord(false);

	return false;
}

/* ArchivePanel::mapOpenDb2
 * Opens the currently selected entry in Doom Builder 2 if it is a
 * valid map entry (either a map header or archive in maps/)
 *******************************************************************/
bool ArchivePanel::mapOpenDb2()
{
	// Get first selected entry
	ArchiveEntry* entry = entry_list->getEntry(entry_list->getFirstSelected());

	// Do open in db2
	return EntryOperations::openMapDB2(entry);
}


/* ArchivePanel::openDir
 * Opens the given directory.
 *******************************************************************/
bool ArchivePanel::openDir(ArchiveTreeNode* dir)
{
	return entry_list->setDir(dir);
}

/* ArchivePanel::openEntry
 * Shows the appropriate entry area and sends the given entry to it.
 * If [force] is true, the entry is opened even if it is already open
 *******************************************************************/
bool ArchivePanel::openEntry(ArchiveEntry* entry, bool force)
{
	// Null entry, do nothing
	if (!entry)
	{
		wxLogMessage("Warning: NULL entry focused in the list");
		return false;
	}

	// Do nothing if the entry is already open
	if (cur_area->getEntry() == entry && !force)
		return false;

	// Detect entry type if it hasn't been already
	if (entry->getType() == EntryType::unknownType())
		EntryType::detectEntryType(entry);

	// Are we trying to open a directory? This can happen from bookmarks.
	if (entry->getType() == EntryType::folderType())
	{
		// Get directory to open
		ArchiveTreeNode* dir = NULL;

		// Removes starting / from path
		string name = entry->getPath(true);
		if (name.StartsWith("/"))
			name.Remove(0, 1);

		dir = archive->getDir(name, NULL);

		// Check it exists (really should)
		if (!dir)
		{
			wxLogMessage("Error: Trying to open nonexistant directory %s", name);
			return false;
		}
		entry_list->setDir(dir);
	}
	else
	{
		// Save changes if needed
		saveEntryChanges();

		// Close the current entry
		cur_area->closeEntry();

		// Get the appropriate entry panel for the entry's type
		EntryPanel* new_area = default_area;
		if (entry->getType() == EntryType::mapMarkerType())
			new_area = map_area;
		else if (!entry->getType()->getEditor().Cmp("gfx"))
			new_area = gfx_area;
		else if (!entry->getType()->getEditor().Cmp("palette"))
			new_area = pal_area;
		else if (!entry->getType()->getEditor().Cmp("ansi"))
			new_area = ansi_area;
		else if (!entry->getType()->getEditor().Cmp("text"))
			new_area = text_area;
		else if (!entry->getType()->getEditor().Cmp("animated"))
			new_area = animated_area;
		else if (!entry->getType()->getEditor().Cmp("switches"))
			new_area = switches_area;
		else if (!entry->getType()->getEditor().Cmp("audio"))
			new_area = audio_area;
		else if (!entry->getType()->getEditor().Cmp("default"))
			new_area = default_area;
		else
			wxLogMessage("Entry editor %s does not exist, using default editor", entry->getType()->getEditor());

		// Load the entry into the panel
		if (!new_area->openEntry(entry))
		{
			wxMessageBox(S_FMT("Error loading entry:\n%s", Global::error), "Error", wxOK|wxICON_ERROR);
		}

		// Show the new entry panel
		if (!showEntryPanel(new_area))
			return false;
		else
			new_area->updateToolbar();
	}
	return true;
}

/* ArchivePanel::openEntryAsText
 * Opens [entry] in the text editor panel
 *******************************************************************/
bool ArchivePanel::openEntryAsText(ArchiveEntry* entry)
{
	// Check entry was given
	if (!entry)
		return false;

	// Load the current entry into the panel
	if (!text_area->openEntry(entry))
	{
		wxMessageBox(S_FMT("Error loading entry:\n%s", Global::error), "Error", wxOK|wxICON_ERROR);
	}

	// Show the text entry panel
	if (!showEntryPanel(text_area))
		return false;

	return true;
}

/* ArchivePanel::openEntryAsHex
 * Opens [entry] in the hex editor panel
 *******************************************************************/
bool ArchivePanel::openEntryAsHex(ArchiveEntry* entry)
{
	// Check entry was given
	if (!entry)
		return false;

	// Load the current entry into the panel
	if (!hex_area->openEntry(entry))
	{
		wxMessageBox(S_FMT("Error loading entry:\n%s", Global::error), "Error", wxOK|wxICON_ERROR);
	}

	// Show the text entry panel
	if (!showEntryPanel(hex_area))
		return false;

	return true;
}

/* ArchivePanel::reloadCurrentPanel
 * If only one entry is selected, force its reload
 *******************************************************************/
bool ArchivePanel::reloadCurrentPanel()
{
	// Do nothing if there are multiple entries
	if (cur_area == default_area)
		return false;

	return openEntry(cur_area->getEntry(), true);
}

/* ArchivePanel::focusOnEntry
 * Makes sure the list view display shows the given entry
 *******************************************************************/
void ArchivePanel::focusOnEntry(ArchiveEntry* entry)
{
	if (entry)
	{
		// Do we need to change directory?
		if (entry->getParentDir() != entry_list->getCurrentDir())
			entry_list->setDir(entry->getParentDir());

		// Now focus on the entry if it is listed
		for (long index = 0; index < entry_list->GetItemCount(); ++index)
		{
			if (entry == entry_list->getEntry(index))
			{
				entry_list->focusOnIndex(index);
				return;
			}
		}
	}
}

/* ArchivePanel::showEntryPanel
 * Show an entry panel appropriate to the current entry
 *******************************************************************/
bool ArchivePanel::showEntryPanel(EntryPanel* new_area, bool ask_save)
{
	// Save any changes if needed
	saveEntryChanges();

	// Get the panel sizer
	wxSizer* sizer = GetSizer();

	// If the new panel is different than the current, swap them
	if (new_area != cur_area)
	{
		cur_area->Show(false);				// Hide current
		cur_area->removeCustomMenu();		// Remove current custom menu (if any)
		cur_area->removeCustomToolBar();	// Remove current custom toolbar (if any)
		sizer->Replace(cur_area, new_area);	// Swap the panels
		cur_area = new_area;				// Set the new panel to current
		cur_area->Show(true);				// Show current

		// Add the current panel's custom menu and toolbar if needed
		cur_area->addCustomMenu();
		cur_area->addCustomToolBar();

		// Set panel undo manager
		cur_area->setUndoManager(undo_manager);

		// Update panel layout
		Layout();
		theMainWindow->Update();
		theMainWindow->Refresh();
		theMainWindow->Update();
	}
	else if (!cur_area->IsShown())
	{
		// Show current
		cur_area->Show();
	}

	return true;
}

/* ArchivePanel::refreshPanel
 * Refreshes everything on the panel
 *******************************************************************/
void ArchivePanel::refreshPanel()
{
	// Refresh entry list
	entry_list->applyFilter();

	// Refresh current entry panel
	cur_area->refreshPanel();

	// Refresh entire panel
	Update();
	Refresh();
}

/* ArchivePanel::handleAction
 * Handles the action [id]. Returns true if the action was handled,
 * false otherwise
 *******************************************************************/
bool ArchivePanel::handleAction(string id)
{
	// Don't handle actions if hidden
	if (!IsShown())
		return false;

	// We're only interested in "arch_" actions (and some others)
	if (!id.StartsWith("arch_") && !id.StartsWith("pmap_"))
		return false;


	// *************************************************************
	// ARCHIVE MENU
	// *************************************************************

	// Archive->New->Entry
	else if (id == "arch_newentry")
		newEntry();

	// Archive->New->Entry variants
	else if (id == "arch_newpalette")	newEntry(ENTRY_PALETTE);
	else if (id == "arch_newanimated")	newEntry(ENTRY_ANIMATED);
	else if (id == "arch_newswitches")	newEntry(ENTRY_SWITCHES);

	// Archive->New->Directory
	else if (id == "arch_newdir")
		newDirectory();

	// Archive->Import Files
	else if (id == "arch_importfiles")
		importFiles();

	// Archive->Texture Editor
	else if (id == "arch_texeditor")
		theMainWindow->openTextureEditor(archive);

	else if (id == "arch_mapeditor")
		theMainWindow->openMapEditor(archive);

	// Archive->Convert To...
	else if (id == "arch_convert")
		convertArchiveTo();

	// Archive->Maintenance->Remove Unused Patches
	else if (id == "arch_clean_patches")
		ArchiveOperations::removeUnusedPatches(archive);

	// Archive->Maintenance->Remove Unused Textures
	else if (id == "arch_clean_textures")
		ArchiveOperations::removeUnusedTextures(archive);

	// Archive->Maintenance->Remove Unused Flats
	else if (id == "arch_clean_flats")
		ArchiveOperations::removeUnusedFlats(archive);

	// Archive->Maintenance->Check Duplicate Entry Names
	else if (id == "arch_check_duplicates")
		ArchiveOperations::checkDuplicateEntryNames(archive);

	// Archive->Maintenance->Check Duplicate Entry Names
	else if (id == "arch_check_duplicates2")
		ArchiveOperations::checkDuplicateEntryContent(archive);

	// Archive->Maintenance->Check Duplicate Entry Names
	else if (id == "arch_clean_iwaddupes")
		ArchiveOperations::removeEntriesUnchangedFromIWAD(archive);

	// Archive->Maintenance->Replace in Maps
	else if (id == "arch_replace_maps")
	{
		MapReplaceDialog dlg(this, archive);
		dlg.ShowModal();
	}


	// *************************************************************
	// ENTRY MENU
	// *************************************************************

	// Entry->Rename
	else if (id == "arch_entry_rename")
		renameEntry();

	// Entry->Rename Each
	else if (id == "arch_entry_rename_each")
		renameEntry(true);

	// Entry->Delete
	else if (id == "arch_entry_delete")
		deleteEntry();

	else if (id == "arch_entry_revert")
		revertEntry();

	// Entry->Cut
	else if (id == "arch_entry_cut")
		cutEntry();

	// Entry->Copy
	else if (id == "arch_entry_copy")
		copyEntry();

	// Entry->Paste
	else if (id == "arch_entry_paste")
		pasteEntry();

	// Entry->Move Up
	else if (id == "arch_entry_moveup")
		moveUp();

	// Entry->Move Down
	else if (id == "arch_entry_movedown")
		moveDown();

	// Entry->Sort
	else if (id == "arch_entry_sort")
		sort();

	// Entry->Bookmark
	else if (id == "arch_entry_bookmark")
		bookmark();

	// Open in Tab
	else if (id == "arch_entry_opentab")
		openTab();

	// CRC-32
	else if (id == "arch_entry_crc32")
		crc32();

	// Entry->Convert To...
	else if (id == "arch_entry_convert")
		convertEntryTo();

	// Entry->Import
	else if (id == "arch_entry_import")
		importEntry();

	// Entry->Export
	else if (id == "arch_entry_export")
		exportEntry();

	// Entry->Export As...
	else if (id == "arch_entry_exportas")
		exportEntryAs();



	// Context menu actions
	else if (id == "arch_bas_convertb")
		basConvert(false);
	else if (id == "arch_bas_convertz")
		basConvert(true);
	else if (id == "arch_swan_convert")
		swanConvert();
	else if (id == "arch_gfx_convert")
		gfxConvert();
	else if (id == "arch_gfx_translate")
		gfxRemap();
	else if (id == "arch_gfx_colourise")
		gfxColourise();
	else if (id == "arch_gfx_tint")
		gfxTint();
	else if (id == "arch_gfx_offsets")
		gfxModifyOffsets();
	else if (id == "arch_gfx_addptable")
		EntryOperations::addToPatchTable(entry_list->getSelectedEntries());
	else if (id == "arch_gfx_addtexturex")
		EntryOperations::createTexture(entry_list->getSelectedEntries());
	else if (id == "arch_gfx_exportpng")
		gfxExportPNG();
	else if (id == "arch_gfx_pngopt")
		optimizePNG();
	else if (id == "arch_view_text")
		openEntryAsText(entry_list->getFocusedEntry());
	else if (id == "arch_view_hex")
		openEntryAsHex(entry_list->getFocusedEntry());
	else if (id == "arch_audio_convertdw")
		dSndWavConvert();
	else if (id == "arch_audio_convertwd")
		wavDSndConvert();
	else if (id == "arch_audio_convertmus")
		musMidiConvert();
	else if (id == "arch_scripts_compileacs")
		compileACS();
	else if (id == "arch_scripts_compilehacs")
		compileACS(true);
	else if (id == "arch_texturex_convertzd")
		convertTextures();
	else if (id == "arch_map_opendb2")
		mapOpenDb2();

	// Map Editor Panel
	else if (id == "pmap_open_text")
	{
		// Edit Level Script

		// Get entry to edit
		ArchiveEntry* entry = map_area->getEntry();

		// Open in text editor
		openEntryAsText(entry);
	}

	// Run archive
	else if (id == "arch_run")
	{
		RunDialog dlg(this, archive);
		if (dlg.ShowModal() == wxID_OK)
		{
			string command = dlg.getSelectedCommandLine(archive, "");
			if (!command.IsEmpty())
			{
				// Set working directory
				string wd = wxGetCwd();
				wxSetWorkingDirectory(dlg.getSelectedExeDir());

				// Run
				wxExecute(command, wxEXEC_ASYNC);

				// Restore working directory
				wxSetWorkingDirectory(wd);
			}
		}

		return true;
	}

	// Unknown action
	else
		return false;

	// Action handled, return true
	return true;
}

/* ArchivePanel::onAnnouncement
 * Called when an announcement is recieved from the archive that
 * this ArchivePanel is managing
 *******************************************************************/
void ArchivePanel::onAnnouncement(Announcer* announcer, string event_name, MemChunk& event_data)
{
	// Reset event data for reading
	event_data.seek(0, SEEK_SET);

	// If the archive was saved
	if (announcer == archive && event_name == "saved")
	{
		// Update this tab's name in the parent notebook (if filename was changed)
		wxAuiNotebook* parent = (wxAuiNotebook*)GetParent();
		parent->SetPageText(parent->GetPageIndex(this), archive->getFilename(false));
	}

	// If a directory was added
	if (announcer == archive && event_name == "directory_added")
	{
		// Show path controls (if they aren't already)
		if (!GetSizer()->IsShown(sizer_path_controls))
		{
			sizer_path_controls->Show(true);
			Layout();
		}
	}

	// If an entry was removed
	if (announcer == archive && event_name == "entry_removing")
	{
		// Get entry pointer
		wxUIntPtr ptr;
		event_data.seek(sizeof(int), 0);
		event_data.read(&ptr, sizeof(wxUIntPtr));
		ArchiveEntry* entry = (ArchiveEntry*)wxUIntToPtr(ptr);

		// Close current entry panel if it's entry was removed
		if (currentArea()->getEntry() == entry)
		{
			currentArea()->closeEntry();
			currentArea()->openEntry(NULL);
			currentArea()->Show(false);
		}
	}
}


/*******************************************************************
 * ARCHIVEPANEL STATIC FUNCTIONS
 *******************************************************************/

/* ArchivePanel::createPanelForEntry
 * Creates the appropriate EntryPanel for [entry] and returns it
 *******************************************************************/
EntryPanel* ArchivePanel::createPanelForEntry(ArchiveEntry* entry, wxWindow* parent)
{
	EntryPanel* entry_panel = NULL;

	if (entry->getType() == EntryType::mapMarkerType())
		entry_panel = new MapEntryPanel(parent);
	else if (!entry->getType()->getEditor().Cmp("gfx"))
		entry_panel = new GfxEntryPanel(parent);
	else if (!entry->getType()->getEditor().Cmp("palette"))
		entry_panel = new PaletteEntryPanel(parent);
	else if (!entry->getType()->getEditor().Cmp("ansi"))
		entry_panel = new ANSIEntryPanel(parent);
	else if (!entry->getType()->getEditor().Cmp("text"))
		entry_panel = new TextEntryPanel(parent);
	else if (!entry->getType()->getEditor().Cmp("animated"))
		entry_panel = new AnimatedEntryPanel(parent);
	else if (!entry->getType()->getEditor().Cmp("switches"))
		entry_panel = new SwitchesEntryPanel(parent);
	else if (!entry->getType()->getEditor().Cmp("audio"))
		entry_panel = new AudioEntryPanel(parent);
	else
		entry_panel = new DefaultEntryPanel(parent);

	return entry_panel;
}


/*******************************************************************
 * ARCHIVEPANEL EVENTS
 *******************************************************************/

/* ArchivePanel::onEntryListSelectionChange
 * Called when the selection on the entry list is changed
 *******************************************************************/
void ArchivePanel::onEntryListSelectionChange(wxCommandEvent& e)
{
	// Do nothing if not shown
	if (!IsShown())
		return;

	// Get selected entries
	vector<ArchiveEntry*> selection = entry_list->getSelectedEntries();

	if (selection.size() == 0)
		return;	// If no entries are selected do nothing
	else if (selection.size() == 1)
	{
		// If one entry is selected, open it in the entry area
		openEntry(selection[0]);
	}
	else
	{
		// If multiple entries are selected, show/update the multi entry area
		showEntryPanel(default_area);
		((DefaultEntryPanel*)default_area)->loadEntries(selection);
	}
}

/* ArchivePanel::onEntryListFocusChange
 * Called when the focused item on the entry list is changed
 *******************************************************************/
void ArchivePanel::onEntryListFocusChange(wxListEvent& e)
{
	// Do nothing if not shown
	if (!IsShown())
		return;

	// Get selected entries
	vector<ArchiveEntry*> selection = entry_list->getSelectedEntries();

	if (selection.size() == 0)
		return;	// If no entries are selected do nothing
	else if (selection.size() == 1)
	{
		// If one entry is selected, open it in the entry area
		openEntry(selection[0]);
	}
	else
	{
		// If multiple entries are selected, show/update the multi entry area
		showEntryPanel(default_area);
		((DefaultEntryPanel*)default_area)->loadEntries(selection);
	}
}

/* ArchivePanel::onEntryListRightClick
 * Called when the entry list is right clicked
 *******************************************************************/
void ArchivePanel::onEntryListRightClick(wxListEvent& e)
{
	// Get selected entries
	vector<ArchiveEntry*> selection = entry_list->getSelectedEntries();

	// Check what types exist in the selection
	// TODO: This stuff is absolutely terrible, nicer system needed
	bool gfx_selected = false;
	bool png_selected = false;
	bool bas_selected = false;
	bool wav_selected = false;
	bool dsnd_selected = false;
	bool mus_selected = false;
	bool text_selected = false;
	bool unknown_selected = false;
	bool texturex_selected = false;
	bool modified_selected = false;
	bool map_selected = false;
	bool swan_selected = false;
//	bool rle_selected = false;
	for (size_t a = 0; a < selection.size(); a++)
	{
		// Check for gfx entry
		if (!gfx_selected)
		{
			if (selection[a]->getType()->extraProps().propertyExists("image"))
				gfx_selected = true;
		}
		if (!png_selected)
		{
			if (selection[a]->getType()->getFormat() == "img_png")
				png_selected = true;
		}
		if (!bas_selected)
		{
			if (selection[a]->getType()->getFormat() == "animated" ||
			        selection[a]->getType()->getFormat() == "switches")
				bas_selected = true;
		}
		if (!swan_selected)
		{
			if (selection[a]->getType()->getId() == "swantbls")
				swan_selected = true;
		}
		if (!wav_selected)
		{
			if (selection[a]->getType()->getFormat() == "snd_wav")
				wav_selected = true;
		}
		if (!dsnd_selected)
		{
			if (selection[a]->getType()->getFormat() == "snd_doom" ||
			        selection[a]->getType()->getFormat() == "snd_speaker" ||
			        selection[a]->getType()->getFormat() == "snd_wolf" ||
			        selection[a]->getType()->getFormat() == "snd_doom_mac" ||
			        selection[a]->getType()->getFormat() == "snd_jaguar" ||
			        selection[a]->getType()->getFormat() == "snd_bloodsfx" ||
			        selection[a]->getType()->getFormat() == "snd_voc")
				dsnd_selected = true;
		}
		if (!mus_selected)
		{
			if (selection[a]->getType()->getFormat() == "mus" ||
				selection[a]->getType()->getFormat() == "hmi" ||
				selection[a]->getType()->getFormat() == "hmp" ||
				selection[a]->getType()->getFormat() == "xmi" ||
				selection[a]->getType()->getFormat() == "gmid")
				mus_selected = true;
		}
		if (!text_selected)
		{
			if (selection[a]->getType()->getFormat() == "text")
				text_selected = true;
		}
		if (!unknown_selected)
		{
			if (selection[a]->getType() == EntryType::unknownType())
				unknown_selected = true;
		}
		if (!texturex_selected)
		{
			if (selection[a]->getType()->getFormat() == "texturex")
				texturex_selected = true;
		}
		if (!modified_selected)
		{
			if (selection[a]->getState() == 1)
				modified_selected = true;
		}
		if (!map_selected)
		{
			if (selection[a]->getType() == EntryType::mapMarkerType())
				map_selected = true;
			else if (selection[a]->getParentDir()->getName() == "maps")
				map_selected = true;
		}
#if 0
		if (!rle_selected)
		{
			if (selection[a]->getType()->getFormat() == "misc_rle0")
				rle_selected = true;
		}
#endif
	}

	// Generate context menu
	wxMenu context;
	theApp->getAction("arch_entry_rename")->addToMenu(&context);
	if (selection.size() > 1) theApp->getAction("arch_entry_rename_each")->addToMenu(&context);
	theApp->getAction("arch_entry_delete")->addToMenu(&context);
	if (modified_selected) theApp->getAction("arch_entry_revert")->addToMenu(&context);
	context.AppendSeparator();
	theApp->getAction("arch_entry_cut")->addToMenu(&context);
	theApp->getAction("arch_entry_copy")->addToMenu(&context);
	theApp->getAction("arch_entry_paste")->addToMenu(&context);
	context.AppendSeparator();
	theApp->getAction("arch_entry_import")->addToMenu(&context);
	theApp->getAction("arch_entry_export")->addToMenu(&context);
	context.AppendSeparator();
	theApp->getAction("arch_entry_moveup")->addToMenu(&context);
	theApp->getAction("arch_entry_movedown")->addToMenu(&context);
	theApp->getAction("arch_entry_sort")->addToMenu(&context);
	context.AppendSeparator();
	theApp->getAction("arch_entry_bookmark")->addToMenu(&context);
	theApp->getAction("arch_entry_opentab")->addToMenu(&context);
	theApp->getAction("arch_entry_crc32")->addToMenu(&context);

	// Add custom menu items
	wxMenu* custom;
	if (context_submenus)
	{
		custom = new wxMenu();
	}
	else custom = &context;
	bool ok = cur_area->fillCustomMenu(custom);
	if (context_submenus)
	{
		if (ok) context.AppendSubMenu(custom, cur_area->getCustomMenuName());
		else delete custom;
	}

	// Add Boom Animations/Switches related menu items if they are selected
	if (bas_selected)
	{
		theApp->getAction("arch_bas_convertb")->addToMenu(&context);
		theApp->getAction("arch_bas_convertz")->addToMenu(&context);
	}
	if (swan_selected)
	{
		theApp->getAction("arch_swan_convert")->addToMenu(&context);
	}

	// Add texturex related menu items if needed
	if (texturex_selected)
		theApp->getAction("arch_texturex_convertzd")->addToMenu(&context);

	// 'View As' menu
	if (context_submenus)
	{
		wxMenu* viewas = new wxMenu();
		context.AppendSubMenu(viewas, "View As");
		theApp->getAction("arch_view_text")->addToMenu(viewas, "Text");
		theApp->getAction("arch_view_hex")->addToMenu(viewas, "Hex");
	}
	else
	{
		context.AppendSeparator();
		theApp->getAction("arch_view_text")->addToMenu(&context);
		theApp->getAction("arch_view_hex")->addToMenu(&context);
	}

	// Add gfx-related menu items if gfx are selected (multi-select only)
	if (gfx_selected && selection.size() > 1)
	{
		wxMenu* gfx;
		if (context_submenus)
		{
			gfx = new wxMenu();
			context.AppendSubMenu(gfx, "Graphics");
		}
		else
		{
			context.AppendSeparator();
			gfx = &context;
		}
		theApp->getAction("arch_gfx_convert")->addToMenu(gfx);
		theApp->getAction("arch_gfx_translate")->addToMenu(gfx);
		theApp->getAction("arch_gfx_colourise")->addToMenu(gfx);
		theApp->getAction("arch_gfx_tint")->addToMenu(gfx);
		theApp->getAction("arch_gfx_offsets")->addToMenu(gfx);
		theApp->getAction("arch_gfx_addptable")->addToMenu(gfx);
		theApp->getAction("arch_gfx_addtexturex")->addToMenu(gfx);
		theApp->getAction("arch_gfx_exportpng")->addToMenu(gfx);
		if (png_selected)
			theApp->getAction("arch_gfx_pngopt")->addToMenu(gfx);
	}

	// Add Audio related menu items if needed
	if (wav_selected || dsnd_selected || mus_selected)
	{
		wxMenu* audio;
		if (context_submenus)
		{
			audio = new wxMenu();
			context.AppendSubMenu(audio, "Audio");
		}
		else
		{
			context.AppendSeparator();
			audio = &context;
		}
		if (wav_selected)
			theApp->getAction("arch_audio_convertwd")->addToMenu(audio);
		if (dsnd_selected)
			theApp->getAction("arch_audio_convertdw")->addToMenu(audio);
		if (mus_selected)
			theApp->getAction("arch_audio_convertmus")->addToMenu(audio);
	}

	// Add script related menu items if needed
	if (text_selected || unknown_selected)
	{
		wxMenu* scripts;
		if (context_submenus)
		{
			scripts = new wxMenu();
			context.AppendSubMenu(scripts, "Scripts");
		}
		else
		{
			context.AppendSeparator();
			scripts = &context;
		}
		theApp->getAction("arch_scripts_compileacs")->addToMenu(scripts);
		theApp->getAction("arch_scripts_compilehacs")->addToMenu(scripts);
	}

	// Add map related menu items if needed
	if (map_selected)
	{
		// 'Open in Doom Builder 2' (windows-only)
#ifdef __WXMSW__
		theApp->getAction("arch_map_opendb2")->addToMenu(&context);
#endif
	}

	// Popup the context menu
	PopupMenu(&context);
}

/* ArchivePanel::onEntryListKeyDown
 * Called when a key is pressed on the entry list
 *******************************************************************/
void ArchivePanel::onEntryListKeyDown(wxKeyEvent& e)
{
	// Check if keypress matches any keybinds
	wxArrayString binds = KeyBind::getBinds(KeyBind::asKeyPress(e.GetKeyCode(), e.GetModifiers()));

	// Go through matching binds
	for (unsigned a = 0; a < binds.size(); a++)
	{
		string name = binds[a];

		// --- General ---

		// Copy
		if (name == "copy")
		{
			copyEntry();
			return;
		}

		// Cut
		else if (name == "cut")
		{
			cutEntry();
			return;
		}

		// Paste
		else if (name == "paste")
		{
			pasteEntry();
			return;
		}

		// Select All
		else if (name == "select_all")
		{
			entry_list->selectAll();
			return;
		}


		// --- Entry list specific ---

		// New Entry
		else if (name == "el_new")
		{
			newEntry();
			return;
		}

		// Rename Entry
		else if (name == "el_rename")
		{
			renameEntry();
			return;
		}

		// Delete Entry
		else if (name == "el_delete")
		{
			deleteEntry();
			return;
		}

		// Move Entry up
		else if (name == "el_move_up")
		{
			moveUp();
			return;
		}

		// Move Entry down
		else if (name == "el_move_down")
		{
			moveDown();
			return;
		}

		// Import to Entry
		else if (name == "el_import")
		{
			importEntry();
			return;
		}

		// Import Files
		else if (name == "el_import_files")
		{
			importFiles();
			return;
		}

		// Export Entry
		else if (name == "el_export")
		{
			exportEntry();
			return;
		}

		// Up directory
		else if (name == "el_up_dir")
		{
			entry_list->goUpDir();
			return;
		}
	}

	// Not handled, send to parent
	e.Skip();
}

/* ArchivePanel::onEntryListActivated
 * Called when an item on the entry list is 'activated'
 * (via double-click or enter)
 *******************************************************************/
void ArchivePanel::onEntryListActivated(wxListEvent& e)
{
	ArchiveEntry* entry = entry_list->getFocusedEntry();

	if (!entry)
		return;

	// Archive
	if (entry->getType()->getFormat().substr(0, 8) == "archive_")
		theArchiveManager->openArchive(entry);

	// Texture list
	else if (entry->getType()->getFormat() == "texturex" ||
		     entry->getType() == EntryType::getType("pnames") ||
	         entry->getType() == EntryType::getType("zdtextures"))
		theMainWindow->openTextureEditor(archive, entry);

	// Map
	// TODO: Needs to filter the game/port lists in the dialog by the map format
	else if (entry->getType() == EntryType::mapMarkerType())
	{
		// Open map editor config dialog
		MapEditorConfigDialog dlg(this, archive, false);
		if (dlg.ShowModal() == wxID_OK)
		{
			Archive::mapdesc_t info = archive->getMapInfo(entry);

			// Check selected game configuration is ok
			if (!dlg.configMatchesMap(info))
				wxMessageBox("Selected Game Configuration does not match the map format", "Error", wxICON_ERROR);
			else
			{
				// Load game configuration
				theGameConfiguration->openConfig(dlg.selectedGame(), dlg.selectedPort(), info.format);

				// Attempt to open map
				if (theMapEditor->openMap(info))
					theMapEditor->Show();
				else
				{
					theMapEditor->Hide();
					wxMessageBox(S_FMT("Unable to open map %s: %s", entry->getName(), Global::error), "Invalid map error", wxICON_ERROR);
				}
			}
		}
	}

	// Other entry
	else if (entry->getType() != EntryType::folderType())
		theMainWindow->openEntry(entry);

	e.Skip();
}

/* ArchivePanel::onMEPEditAsText
 * Called when the 'Edit Level Script' button is clicked on the map
 * entry panel - opens the entry in the text editor panel
 *******************************************************************/
void ArchivePanel::onMEPEditAsText(wxCommandEvent& e)
{
	// Get entry to edit
	ArchiveEntry* entry = map_area->getEntry();

	// Open in text editor
	openEntryAsText(entry);
}

/* ArchivePanel::onDEPEditAsText
 * Called when the 'Edit as Text' button is clicked on the default
 * entry panel - opens the entry in the text editor panel
 *******************************************************************/
void ArchivePanel::onDEPEditAsText(wxCommandEvent& e)
{
	// Get entry to edit
	ArchiveEntry* entry = default_area->getEntry();

	// Open in text editor
	openEntryAsText(entry);
}

/* ArchivePanel::onDEPEViewAsHex
 * Called when the 'View as Hex' button is clicked on the default
 * entry panel - opens the entry in the hex editor panel
 *******************************************************************/
void ArchivePanel::onDEPViewAsHex(wxCommandEvent& e)
{
	// Get entry to view
	ArchiveEntry* entry = default_area->getEntry();

	// Open in hex editor
	openEntryAsHex(entry);
}

/* ArchivePanel::onTextFilterChanged
 * Called when the filter text is changed
 *******************************************************************/
void ArchivePanel::onTextFilterChanged(wxCommandEvent& e)
{
	// Get category string to filter by
	string category = "";
	if (choice_category->GetSelection() > 0)
		category = choice_category->GetStringSelection();

	// Filter the entry list
	entry_list->filterList(text_filter->GetValue(), category);

	e.Skip();
}

/* ArchivePanel::onChoiceCategoryChanged
 * Called when the 'Category' choice is changed
 *******************************************************************/
void ArchivePanel::onChoiceCategoryChanged(wxCommandEvent& e)
{
	// Get category string to filter by
	string category = "";
	if (choice_category->GetSelection() > 0)
		category = choice_category->GetStringSelection();

	// Filter the entry list
	entry_list->filterList(text_filter->GetValue(), category);

	e.Skip();
}

/* ArchivePanel::onDirChanged
 * Called when the entry list directory is changed
 *******************************************************************/
void ArchivePanel::onDirChanged(wxCommandEvent& e)
{
	// Get directory
	ArchiveTreeNode* dir = entry_list->getCurrentDir();

	if (!dir->getParent())
	{
		// Root dir
		label_path->SetLabel("Path:");
		btn_updir->Enable(false);
	}
	else
	{
		// Setup path string
		string path = dir->getPath();
		path.Remove(0, 1);
		path.Prepend("Path: ");

		label_path->SetLabel(path);
		btn_updir->Enable(true);
	}
}

/* ArchivePanel::onBtnUpDir
 * Called when the 'Up Directory' button is clicked
 *******************************************************************/
void ArchivePanel::onBtnUpDir(wxCommandEvent& e)
{
	// Go up a directory in the entry list
	entry_list->goUpDir();
}


/*******************************************************************
 * ENTRYDATAUS CLASS FUNCTIONS
 *******************************************************************/

/* EntryDataUS::swapData
 * Swaps data between the entry and the undo step
 *******************************************************************/
bool EntryDataUS::swapData()
{
	//wxLogMessage("Entry data swap...");

	// Get parent dir
	ArchiveTreeNode* dir = archive->getDir(path);
	if (dir)
	{
		// Get entry
		ArchiveEntry* entry = dir->getEntry(index);

		// Backup data
		MemChunk temp_data;
		temp_data.importMem(entry->getData(), entry->getSize());
		//wxLogMessage("Backup current data, size %d", entry->getSize());

		// Restore entry data
		if (data.getSize() == 0)
		{
			entry->clearData();
			//wxLogMessage("Clear entry data");
		}
		else
		{
			entry->importMemChunk(data);
			//wxLogMessage("Restored entry data, size %d", data.getSize());
		}

		// Store previous entry data
		if (temp_data.getSize() > 0)
			data.importMem(temp_data.getData(), temp_data.getSize());
		else
			data.clear();

		return true;
	}

	return false;
}


/*******************************************************************
 * EXTRA CONSOLE COMMANDS
 *******************************************************************/

// I'd love to put them in their own file, but attempting to do so
// results in a circular include nightmare and nothing works anymore.
#include "ConsoleHelpers.h"
#include "Console.h"
#include "MainApp.h"
Archive* CH::getCurrentArchive()
{
	if (theMainWindow)
	{
		if (theMainWindow->getArchiveManagerPanel())
		{
			return theMainWindow->getArchiveManagerPanel()->currentArchive();
		}
	}
	return NULL;
}

ArchivePanel* CH::getCurrentArchivePanel()
{
	ArchiveManagerPanel* archie = theMainWindow->getArchiveManagerPanel();
	if (archie)
	{
		if (archie->isArchivePanel(archie->currentTabIndex()))
			return (ArchivePanel*)(archie->currentPanel());
	}
	return NULL;
}

CONSOLE_COMMAND(palconv, 0, false)
{
	ArchivePanel* meep = CH::getCurrentArchivePanel();
	if (meep)
	{
		meep->palConvert();
		meep->reloadCurrentPanel();
	}
}

CONSOLE_COMMAND(palconv64, 0, false)
{
	ArchivePanel* meep = CH::getCurrentArchivePanel();
	if (meep)
	{
		// Get the entry index of the last selected list item
		ArchiveEntry* pal = meep->currentEntry();
		const uint8_t* source = pal->getData(true);
		uint8_t* dest = new uint8_t[(pal->getSize() / 2) * 3];
		for (size_t i = 0; i < pal->getSize() / 2; ++i)
		{
			uint8_t r, g, b;
			uint16_t col = READ_B16(source, 2*i);
			r = (col & 0xF800) >> 8;
			g = (col & 0x07C0) >> 3;
			b = (col & 0x003E) << 2;
			dest[(3*i)+0] = r;
			dest[(3*i)+1] = g;
			dest[(3*i)+2] = b;
		}
		pal->importMem(dest, (pal->getSize()/2)*3);
		theActivePanel->callRefresh();
		delete[] dest;
	}
}

CONSOLE_COMMAND(palconvpsx, 0, false)
{
	ArchivePanel* meep = CH::getCurrentArchivePanel();
	if (meep)
	{
		// Get the entry index of the last selected list item
		ArchiveEntry* pal = meep->currentEntry();
		const uint8_t* source = pal->getData(true);
		uint8_t* dest = new uint8_t[(pal->getSize() / 2) * 3];
		for (size_t i = 0; i < pal->getSize() / 2; ++i)
		{
			// A1 B5 G5 R5, LE
			uint8_t a, r, g, b;
			uint16_t col = READ_L16(source, 2*i);
			a = (col & 0x8000) >> 15;
			b = (col & 0x7C00) >> 10;
			g = (col & 0x03E0) >>  5;
			r = (col & 0x001F);
			r = (r << 3) | (r >> 2);
			g = (g << 3) | (g >> 2);
			b = (b << 3) | (b >> 2);
			dest[(3*i)+0] = r;
			dest[(3*i)+1] = g;
			dest[(3*i)+2] = b;
		}
		pal->importMem(dest, (pal->getSize()/2)*3);
		theActivePanel->callRefresh();
		delete[] dest;
	}
}

CONSOLE_COMMAND(vertex32x, 0, false)
{
	ArchivePanel* meep = CH::getCurrentArchivePanel();
	if (meep)
	{
		// Get the entry index of the last selected list item
		ArchiveEntry* v32x = meep->currentEntry();
		const uint8_t* source = v32x->getData(true);
		uint8_t* dest = new uint8_t[v32x->getSize() / 2];
		for (size_t i = 0; i < v32x->getSize() / 4; ++i)
		{
			dest[2*i+0] = source[4*i+1];
			dest[2*i+1] = source[4*i+0];
		}
		v32x->importMem(dest, v32x->getSize()/2);
		theActivePanel->callRefresh();
		delete[] dest;
	}
}

CONSOLE_COMMAND(vertexpsx, 0, false)
{
	ArchivePanel* meep = CH::getCurrentArchivePanel();
	if (meep)
	{
		// Get the entry index of the last selected list item
		ArchiveEntry* vpsx = meep->currentEntry();
		const uint8_t* source = vpsx->getData(true);
		uint8_t* dest = new uint8_t[vpsx->getSize() / 2];
		for (size_t i = 0; i < vpsx->getSize() / 4; ++i)
		{
			dest[2*i+0] = source[4*i+2];
			dest[2*i+1] = source[4*i+3];
		}
		vpsx->importMem(dest, vpsx->getSize()/2);
		theActivePanel->callRefresh();
		delete[] dest;
	}
}

CONSOLE_COMMAND(lightspsxtopalette, 0, false)
{
	ArchivePanel* meep = CH::getCurrentArchivePanel();
	if (meep)
	{
		// Get the entry index of the last selected list item
		ArchiveEntry* lights = meep->currentEntry();
		const uint8_t* source = lights->getData(true);
		size_t entries = lights->getSize() / 4;
		uint8_t* dest = new uint8_t[entries * 3];
		for (size_t i = 0; i < entries; ++i)
		{
			dest[3*i+0] = source[4*i+0];
			dest[3*i+1] = source[4*i+1];
			dest[3*i+2] = source[4*i+2];
		}
		lights->importMem(dest, entries * 3);
		theActivePanel->callRefresh();
		delete[] dest;
	}
}


vector<ArchiveEntry*> Console_SearchEntries(string name)
{
	vector<ArchiveEntry*> entries;
	Archive* archive = theMainWindow->getCurrentArchive();
	ArchivePanel* panel = CH::getCurrentArchivePanel();

	if (archive)
	{
		Archive::search_options_t options;
		options.search_subdirs = true;
		if (panel)
		{
			options.dir = panel->currentDir();
		}
		options.match_name = name;
		entries = archive->findAll(options);
	}
	return entries;
}

CONSOLE_COMMAND(find, 1, true)
{
	vector<ArchiveEntry*> entries = Console_SearchEntries(args[0]);

	string message;
	size_t count = entries.size();
	if (count > 0)
	{
		for (size_t i = 0; i < count; ++i)
		{
			message += entries[i]->getPath(true) + "\n";
		}
	}
	wxLogMessage(S_FMT("Found %i entr%s", count, count==1?"y":"ies\n") + message);
}

CONSOLE_COMMAND(ren, 2, true)
{
	Archive* archive = theMainWindow->getCurrentArchive();
	vector<ArchiveEntry*> entries = Console_SearchEntries(args[0]);
	if (entries.size() > 0)
	{
		size_t count = 0;
		for (size_t i = 0; i < entries.size(); ++i)
		{
			// Rename filter logic
			string newname = entries[i]->getName();
			for (unsigned c = 0; c < args[1].size(); c++)
			{
				// Check character
				if (args[1][c] == '*')
					continue;					// Skip if *
				else
				{
					// First check that we aren't past the end of the name
					if (c >= newname.size())
					{
						// If we are, pad it with spaces
						while (newname.size() <= c)
							newname += " ";
					}

					// Replace character
					newname[c] = args[1][c];
				}
			}

			if (archive->renameEntry(entries[i], newname))
				++count;
		}
		wxLogMessage("Renamed %i entr%s", count, count==1?"y":"ies");
	}
}

CONSOLE_COMMAND(cd, 1, true)
{
	Archive* current = theMainWindow->getCurrentArchive();
	ArchivePanel* panel = CH::getCurrentArchivePanel();

	if (current && panel)
	{
		ArchiveTreeNode* dir = panel->currentDir();
		ArchiveTreeNode* newdir = current->getDir(args[0], dir);
		if (newdir == NULL)
		{
			if (args[0].Matches(".."))
				newdir = (ArchiveTreeNode*) dir->getParent();
			else if (args[0].Matches("/") || args[0].Matches("\\"))
				newdir = current->getRoot();
		}

		if (newdir)
		{
			panel->openDir(newdir);
		}
		else
		{
			wxLogMessage("Error: Trying to open nonexistant directory %s", args[0]);
		}

	}
}

CONSOLE_COMMAND(run, 1, true)
{
	MemChunk mc;
	// Try to run a batch command file
	if (wxFile::Exists(args[0]))
	{
		if (!mc.importFile(args[0]))
			return;
	}
	else
	{
		// Try to run a batch command lump
		vector<ArchiveEntry*> entries = Console_SearchEntries(args[0]);
		if (entries.size() > 0)
		{
			if (!mc.importMem(entries[0]->getData(), entries[0]->getSize()))
				return;
		}
	}

	Tokenizer t;
	if (!t.openMem(&mc, args[0]))
		return;

	string cmdline, s;
	uint32_t line = t.lineNo();

	// There's probably a better way to slice the chunk into lines...
	while (1 + t.tokenEnd() < mc.getSize())
	{
		s = t.getToken() + " ";
		if (t.lineNo() != line || 1 + t.tokenEnd() >= mc.getSize())
		{
			theConsole->execute(cmdline);
			cmdline.Empty();
			line = t.lineNo();
		};
		cmdline += s;
	};
}
