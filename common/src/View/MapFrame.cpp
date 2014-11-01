/*
 Copyright (C) 2010-2014 Kristian Duske
 
 This file is part of TrenchBroom.
 
 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 
 TrenchBroom is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
 */

#include "MapFrame.h"

#include "TrenchBroomApp.h"
#include "Preferences.h"
#include "PreferenceManager.h"
#include "IO/DiskFileSystem.h"
#include "Model/Node.h"
#include "Model/NodeCollection.h"
#include "Model/PointFile.h"
#include "View/ActionManager.h"
#include "View/Autosaver.h"
#include "View/CachingLogger.h"
#include "View/CommandIds.h"
#include "View/Console.h"
#include "View/Grid.h"
#include "View/MapDocument.h"
#include "View/SwitchableMapView.h"

#include <wx/clipbrd.h>
#include <wx/display.h>
#include <wx/filedlg.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/timer.h>

#include <cassert>

namespace TrenchBroom {
    namespace View {
        MapFrame::MapFrame() :
        wxFrame(NULL, wxID_ANY, "MapFrame"),
        m_frameManager(NULL),
        m_autosaver(NULL),
        m_autosaveTimer(NULL),
        m_console(NULL) {}
        
        MapFrame::MapFrame(FrameManager* frameManager, MapDocumentSPtr document) :
        wxFrame(NULL, wxID_ANY, "MapFrame"),
        m_frameManager(NULL),
        m_autosaver(NULL),
        m_autosaveTimer(NULL),
        m_console(NULL) {
            Create(frameManager, document);
        }
        
        void MapFrame::Create(FrameManager* frameManager, MapDocumentSPtr document) {
            assert(frameManager != NULL);
            assert(document != NULL);
            
            m_frameManager = frameManager;
            m_document = document;
            m_autosaver = new Autosaver(m_document);

            createGui();
            createMenuBar();
            
            m_document->setParentLogger(logger());

            m_autosaveTimer = new wxTimer(this);
            m_autosaveTimer->Start(1000);
            
            bindObservers();
            bindEvents();
        }
        
        MapFrame::~MapFrame() {
            unbindObservers();
            
            delete m_autosaveTimer;
            m_autosaveTimer = NULL;
            
            delete m_autosaver;
            m_autosaver = NULL;
        }

        void MapFrame::positionOnScreen(wxFrame* reference) {
            const wxDisplay display;
            const wxRect displaySize = display.GetClientArea();
            if (reference == NULL) {
                SetSize(std::min(displaySize.width, 1024), std::min(displaySize.height, 768));
                CenterOnScreen();
            } else {
                wxPoint position = reference->GetPosition();
                position.x += 23;
                position.y += 23;
                
                if (displaySize.GetBottom() - position.x < 100 ||
                    displaySize.GetRight() - position.y < 70)
                    position = displaySize.GetTopLeft();
                
                SetPosition(position);
                SetSize(std::min(displaySize.GetRight() - position.x, 1024), std::min(displaySize.GetBottom() - position.y, 768));
            }
        }

        Logger* MapFrame::logger() const {
            return m_console;
        }
        
        bool MapFrame::newDocument(Model::GamePtr game, const Model::MapFormat::Type mapFormat) {
            if (!confirmOrDiscardChanges())
                return false;
            m_document->newDocument(MapDocument::DefaultWorldBounds, game, mapFormat);
            return true;
        }
        
        bool MapFrame::openDocument(Model::GamePtr game, const IO::Path& path) {
            if (!confirmOrDiscardChanges())
                return false;
            m_document->loadDocument(MapDocument::DefaultWorldBounds, game, path);
            return true;
        }

        bool MapFrame::saveDocument() {
            try {
                const IO::Path& path = m_document->path();
                if (path.isAbsolute() && IO::Disk::fileExists(IO::Disk::fixPath(path))) {
                    m_document->saveDocument();
                    logger()->info("Saved " + m_document->path().asString());
                    return true;
                }
                return saveDocumentAs();
            } catch (FileSystemException e) {
                ::wxMessageBox(e.what(), "", wxOK | wxICON_ERROR, this);
                return false;
            } catch (...) {
                ::wxMessageBox("Unknown error while saving " + m_document->path().asString(), "", wxOK | wxICON_ERROR, this);
                return false;
            }
        }
        
        bool MapFrame::saveDocumentAs() {
            try {
                wxFileDialog saveDialog(this, "Save map file", "", "", "Map files (*.map)|*.map", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
                if (saveDialog.ShowModal() == wxID_CANCEL)
                    return false;
                
                const IO::Path path(saveDialog.GetPath().ToStdString());
                m_document->saveDocumentAs(path);
                logger()->info("Saved " + m_document->path().asString());
                return true;
            } catch (FileSystemException e) {
                ::wxMessageBox(e.what(), "", wxOK | wxICON_ERROR, this);
                return false;
            } catch (...) {
                ::wxMessageBox("Unknown error while saving " + m_document->filename(), "", wxOK | wxICON_ERROR, this);
                return false;
            }
        }

        bool MapFrame::confirmOrDiscardChanges() {
            if (!m_document->modified())
                return true;
            const int result = ::wxMessageBox(m_document->filename() + " has been modified. Do you want to save the changes?", "TrenchBroom", wxYES_NO | wxCANCEL, this);
            switch (result) {
                case wxYES:
                    return saveDocument();
                case wxNO:
                    return true;
                default:
                    return false;
            }
        }

        void MapFrame::updateTitle() {
#ifdef __APPLE__
            SetTitle(m_document->filename());
            OSXSetModified(m_document->modified());
#else
            SetTitle(wxString(m_document->filename()) + wxString(m_document->modified() ? "*" : ""));
#endif
            SetRepresentedFilename(m_document->path().asString());
        }

        void MapFrame::rebuildMenuBar() {
            wxMenuBar* oldMenuBar = GetMenuBar();
            
            const ActionManager& actionManager = ActionManager::instance();
            wxMenu* recentDocumentsMenu = actionManager.findRecentDocumentsMenu(oldMenuBar);
            assert(recentDocumentsMenu != NULL);
            
            TrenchBroomApp& app = TrenchBroomApp::instance();
            app.removeRecentDocumentMenu(recentDocumentsMenu);
            
            SetMenuBar(NULL);
            delete oldMenuBar;
            
            createMenuBar();
        }
        
        void MapFrame::createMenuBar() {
            const ActionManager& actionManager = ActionManager::instance();
            wxMenuBar* menuBar = actionManager.createMenuBar();
            SetMenuBar(menuBar);
            
            wxMenu* recentDocumentsMenu = actionManager.findRecentDocumentsMenu(menuBar);
            assert(recentDocumentsMenu != NULL);
            
            TrenchBroomApp& app = TrenchBroomApp::instance();
            app.addRecentDocumentMenu(recentDocumentsMenu);
        }

        void MapFrame::createGui() {
            m_console = new Console(this);
            m_mapView = new SwitchableMapView(this, m_console, m_document);
            
            wxSizer* frameSizer = new wxBoxSizer(wxVERTICAL);
            frameSizer->Add(m_mapView, 1, wxEXPAND);
            frameSizer->Add(m_console, 0, wxEXPAND);
            frameSizer->SetItemMinSize(m_console, wxSize(wxDefaultCoord, 200));
            
            SetSizer(frameSizer);
        }

        void MapFrame::bindObservers() {
            PreferenceManager& prefs = PreferenceManager::instance();
            prefs.preferenceDidChangeNotifier.addObserver(this, &MapFrame::preferenceDidChange);

            m_document->documentWasClearedNotifier.addObserver(this, &MapFrame::documentWasCleared);
            m_document->documentWasNewedNotifier.addObserver(this, &MapFrame::documentDidChange);
            m_document->documentWasLoadedNotifier.addObserver(this, &MapFrame::documentDidChange);
            m_document->documentWasSavedNotifier.addObserver(this, &MapFrame::documentDidChange);
            m_document->documentModificationStateDidChangeNotifier.addObserver(this, &MapFrame::documentModificationStateDidChange);
        }
        
        void MapFrame::unbindObservers() {
            PreferenceManager& prefs = PreferenceManager::instance();
            prefs.preferenceDidChangeNotifier.removeObserver(this, &MapFrame::preferenceDidChange);

            m_document->documentWasClearedNotifier.removeObserver(this, &MapFrame::documentWasCleared);
            m_document->documentWasNewedNotifier.removeObserver(this, &MapFrame::documentDidChange);
            m_document->documentWasLoadedNotifier.removeObserver(this, &MapFrame::documentDidChange);
            m_document->documentWasSavedNotifier.removeObserver(this, &MapFrame::documentDidChange);
            m_document->documentModificationStateDidChangeNotifier.removeObserver(this, &MapFrame::documentModificationStateDidChange);
        }

        void MapFrame::documentWasCleared(View::MapDocument* document) {
            updateTitle();
        }
        
        void MapFrame::documentDidChange(View::MapDocument* document) {
            updateTitle();
            View::TrenchBroomApp::instance().updateRecentDocument(m_document->path());
        }

        void MapFrame::documentModificationStateDidChange() {
            updateTitle();
        }

        void MapFrame::preferenceDidChange(const IO::Path& path) {
            const ActionManager& actionManager = ActionManager::instance();
            if (actionManager.isMenuShortcutPreference(path))
                rebuildMenuBar();
        }

        void MapFrame::bindEvents() {
            Bind(wxEVT_MENU, &MapFrame::OnFileSave, this, wxID_SAVE);
            Bind(wxEVT_MENU, &MapFrame::OnFileSaveAs, this, wxID_SAVEAS);
            Bind(wxEVT_MENU, &MapFrame::OnFileLoadPointFile, this, CommandIds::Menu::FileLoadPointFile);
            Bind(wxEVT_MENU, &MapFrame::OnFileUnloadPointFile, this, CommandIds::Menu::FileUnloadPointFile);
            Bind(wxEVT_MENU, &MapFrame::OnFileClose, this, wxID_CLOSE);

            Bind(wxEVT_MENU, &MapFrame::OnEditUndo, this, wxID_UNDO);
            Bind(wxEVT_MENU, &MapFrame::OnEditRedo, this, wxID_REDO);
            Bind(wxEVT_MENU, &MapFrame::OnEditRepeat, this, CommandIds::Menu::EditRepeat);
            Bind(wxEVT_MENU, &MapFrame::OnEditClearRepeat, this, CommandIds::Menu::EditClearRepeat);
            
            Bind(wxEVT_MENU, &MapFrame::OnEditCut, this, wxID_CUT);
            Bind(wxEVT_MENU, &MapFrame::OnEditCopy, this, wxID_COPY);
            Bind(wxEVT_MENU, &MapFrame::OnEditPaste, this, wxID_PASTE);
            Bind(wxEVT_MENU, &MapFrame::OnEditPasteAtOriginalPosition, this, CommandIds::Menu::EditPasteAtOriginalPosition);
             
            Bind(wxEVT_MENU, &MapFrame::OnEditSelectAll, this, CommandIds::Menu::EditSelectAll);
            Bind(wxEVT_MENU, &MapFrame::OnEditSelectSiblings, this, CommandIds::Menu::EditSelectSiblings);
            Bind(wxEVT_MENU, &MapFrame::OnEditSelectTouching, this, CommandIds::Menu::EditSelectTouching);
            Bind(wxEVT_MENU, &MapFrame::OnEditSelectInside, this, CommandIds::Menu::EditSelectInside);
            Bind(wxEVT_MENU, &MapFrame::OnEditSelectByLineNumber, this, CommandIds::Menu::EditSelectByFilePosition);
            Bind(wxEVT_MENU, &MapFrame::OnEditSelectNone, this, CommandIds::Menu::EditSelectNone);

            Bind(wxEVT_MENU, &MapFrame::OnEditToggleTextureLock, this, CommandIds::Menu::EditToggleTextureLock);
            
            Bind(wxEVT_MENU, &MapFrame::OnViewToggleShowGrid, this, CommandIds::Menu::ViewToggleShowGrid);
            Bind(wxEVT_MENU, &MapFrame::OnViewToggleSnapToGrid, this, CommandIds::Menu::ViewToggleSnapToGrid);
            Bind(wxEVT_MENU, &MapFrame::OnViewIncGridSize, this, CommandIds::Menu::ViewIncGridSize);
            Bind(wxEVT_MENU, &MapFrame::OnViewDecGridSize, this, CommandIds::Menu::ViewDecGridSize);
            Bind(wxEVT_MENU, &MapFrame::OnViewSetGridSize, this, CommandIds::Menu::ViewSetGridSize1, CommandIds::Menu::ViewSetGridSize256);
            
            Bind(wxEVT_MENU, &MapFrame::OnViewMoveCameraToNextPoint, this, CommandIds::Menu::ViewMoveCameraToNextPoint);
            Bind(wxEVT_MENU, &MapFrame::OnViewMoveCameraToPreviousPoint, this, CommandIds::Menu::ViewMoveCameraToPreviousPoint);
            Bind(wxEVT_MENU, &MapFrame::OnViewCenterCameraOnSelection, this, CommandIds::Menu::ViewCenterCameraOnSelection);
            Bind(wxEVT_MENU, &MapFrame::OnViewMoveCameraToPosition, this, CommandIds::Menu::ViewMoveCameraToPosition);

            Bind(wxEVT_UPDATE_UI, &MapFrame::OnUpdateUI, this, wxID_SAVE);
            Bind(wxEVT_UPDATE_UI, &MapFrame::OnUpdateUI, this, wxID_SAVEAS);
            Bind(wxEVT_UPDATE_UI, &MapFrame::OnUpdateUI, this, wxID_CLOSE);
            Bind(wxEVT_UPDATE_UI, &MapFrame::OnUpdateUI, this, wxID_UNDO);
            Bind(wxEVT_UPDATE_UI, &MapFrame::OnUpdateUI, this, wxID_REDO);
            Bind(wxEVT_UPDATE_UI, &MapFrame::OnUpdateUI, this, wxID_CUT);
            Bind(wxEVT_UPDATE_UI, &MapFrame::OnUpdateUI, this, wxID_COPY);
            Bind(wxEVT_UPDATE_UI, &MapFrame::OnUpdateUI, this, wxID_PASTE);
            Bind(wxEVT_UPDATE_UI, &MapFrame::OnUpdateUI, this, wxID_DELETE);
            Bind(wxEVT_UPDATE_UI, &MapFrame::OnUpdateUI, this, CommandIds::Menu::Lowest, CommandIds::Menu::Highest);

            Bind(wxEVT_CLOSE_WINDOW, &MapFrame::OnClose, this);
            Bind(wxEVT_TIMER, &MapFrame::OnAutosaveTimer, this);
        }
        
        void MapFrame::OnFileSave(wxCommandEvent& event) {
            saveDocument();
        }
        
        void MapFrame::OnFileSaveAs(wxCommandEvent& event) {
            saveDocumentAs();
        }
        
        void MapFrame::OnFileLoadPointFile(wxCommandEvent& event) {
            m_document->loadPointFile();
        }
        
        void MapFrame::OnFileUnloadPointFile(wxCommandEvent& event) {
            m_document->unloadPointFile();
        }
        
        void MapFrame::OnFileClose(wxCommandEvent& event) {
            Close();
        }

        void MapFrame::OnEditUndo(wxCommandEvent& event) {
            m_document->undoLastCommand();
        }
        
        void MapFrame::OnEditRedo(wxCommandEvent& event) {
            m_document->redoNextCommand();
        }

        void MapFrame::OnEditRepeat(wxCommandEvent& event) {
            m_document->repeatLastCommands();
        }
        
        void MapFrame::OnEditClearRepeat(wxCommandEvent& event) {
            m_document->clearRepeatableCommands();
        }

        void MapFrame::OnEditCut(wxCommandEvent& event) {
            copyToClipboard();
            Transaction transaction(m_document, "Cut");
            m_document->deleteObjects();
        }
        
        void MapFrame::OnEditCopy(wxCommandEvent& event) {
            copyToClipboard();
        }

        class OpenClipboard {
        public:
            OpenClipboard() {
                if (!wxTheClipboard->IsOpened())
                    wxTheClipboard->Open();
            }
            
            ~OpenClipboard() {
                if (wxTheClipboard->IsOpened())
                    wxTheClipboard->Close();
            }
        };

        void MapFrame::copyToClipboard() {
            OpenClipboard openClipboard;
            if (wxTheClipboard->IsOpened()) {
                String str;
                if (m_document->hasSelectedNodes())
                    str = m_document->serializeSelectedNodes();
                else if (m_document->hasSelectedBrushFaces())
                    str = m_document->serializeSelectedBrushFaces();
                wxTheClipboard->SetData(new wxTextDataObject(str));
            }
        }

        void MapFrame::OnEditPaste(wxCommandEvent& event) {
            Transaction transaction(m_document);
            if (paste() && m_document->hasSelectedNodes()) {
                const BBox3 bounds = m_document->selectionBounds();
                const Vec3 delta = m_mapView->pasteObjectsDelta(bounds);
                m_document->translateObjects(delta);
            }
        }
        
        void MapFrame::OnEditPasteAtOriginalPosition(wxCommandEvent& event) {
            paste();
        }

        bool MapFrame::paste() {
            OpenClipboard openClipboard;
            if (!wxTheClipboard->IsOpened() || !wxTheClipboard->IsSupported(wxDF_TEXT)) {
                logger()->error("Clipboard is empty");
                return false;
            }

            wxTextDataObject textData;
            if (!wxTheClipboard->GetData(textData)) {
                logger()->error("Could not get clipboard contents");
                return false;
            }
            
            const String text = textData.GetText().ToStdString();
            return m_document->paste(text);
        }

        void MapFrame::OnEditSelectAll(wxCommandEvent& event) {
            m_document->selectAllNodes();
        }

        void MapFrame::OnEditSelectSiblings(wxCommandEvent& event) {
            m_document->selectSiblings();
        }

        void MapFrame::OnEditSelectTouching(wxCommandEvent& event) {
            m_document->selectTouching(true);
        }
        
        void MapFrame::OnEditSelectInside(wxCommandEvent& event) {
            m_document->selectInside(true);
        }

        void MapFrame::OnEditSelectByLineNumber(wxCommandEvent& event) {
            const wxString string = wxGetTextFromUser("Enter a comma- or space separated list of line numbers.", "Select by Line Numbers", "", this);
            if (string.empty())
                return;

            std::vector<size_t> positions;
            wxStringTokenizer tokenizer(string, ", ");
            while (tokenizer.HasMoreTokens()) {
                const wxString token = tokenizer.NextToken();
                long position;
                if (token.ToLong(&position) && position > 0) {
                    positions.push_back(static_cast<size_t>(position));
                }
            }
            
            m_document->selectNodesWithFilePosition(positions);
        }

        void MapFrame::OnEditSelectNone(wxCommandEvent& event) {
            m_document->deselectAll();
        }

        void MapFrame::OnEditToggleTextureLock(wxCommandEvent& event) {
            m_document->setTextureLock(!m_document->textureLock());
        }

        void MapFrame::OnViewToggleShowGrid(wxCommandEvent& event) {
            m_document->grid().toggleVisible();
        }
        
        void MapFrame::OnViewToggleSnapToGrid(wxCommandEvent& event) {
            m_document->grid().toggleSnap();
        }
        
        void MapFrame::OnViewIncGridSize(wxCommandEvent& event) {
            m_document->grid().incSize();
        }
        
        void MapFrame::OnViewDecGridSize(wxCommandEvent& event) {
            m_document->grid().decSize();
        }
        
        void MapFrame::OnViewSetGridSize(wxCommandEvent& event) {
            const size_t size = static_cast<size_t>(event.GetId() - CommandIds::Menu::ViewSetGridSize1);
            assert(size < Grid::MaxSize);
            m_document->grid().setSize(size);
        }
        
        void MapFrame::OnViewMoveCameraToNextPoint(wxCommandEvent& event) {
            assert(m_document->isPointFileLoaded());
            
            Model::PointFile* pointFile = m_document->pointFile();
            assert(pointFile->hasNextPoint());
            
            const Vec3f position = pointFile->nextPoint() + Vec3f(0.0f, 0.0f, 16.0f);
            const Vec3f direction = pointFile->currentDirection();
            m_mapView->animateCamera(position, direction, Vec3f::PosZ, 150);
        }
        
        void MapFrame::OnViewMoveCameraToPreviousPoint(wxCommandEvent& event) {
            assert(m_document->isPointFileLoaded());
            
            Model::PointFile& pointFile = m_document->pointFile();
            assert(pointFile.hasPreviousPoint());
            
            const Vec3f position = pointFile.previousPoint() + Vec3f(0.0f, 0.0f, 16.0f);
            const Vec3f direction = pointFile.currentDirection();
            m_mapView->animateCamera(position, direction, Vec3f::PosZ, 150);
        }
        
        void MapFrame::OnViewCenterCameraOnSelection(wxCommandEvent& event) {
            m_mapView->centerCameraOnSelection();
        }
        
        void MapFrame::OnViewMoveCameraToPosition(wxCommandEvent& event) {
            wxTextEntryDialog dialog(this, "Enter a position (x y z) for the camera.", "Move Camera", "0.0 0.0 0.0");
            if (dialog.ShowModal() == wxID_OK) {
                const wxString str = dialog.GetValue();
                const Vec3 position = Vec3::parse(str.ToStdString());
                m_mapView->moveCameraToPosition(position);
            }
        }

        void MapFrame::OnUpdateUI(wxUpdateUIEvent& event) {
            const ActionManager& actionManager = ActionManager::instance();
            
            switch (event.GetId()) {
                case wxID_OPEN:
                case wxID_SAVE:
                case wxID_SAVEAS:
                case wxID_CLOSE:
                    event.Enable(true);
                    break;
                case CommandIds::Menu::FileLoadPointFile:
                    event.Enable(m_document->canLoadPointFile());
                    break;
                case CommandIds::Menu::FileUnloadPointFile:
                    event.Enable(m_document->isPointFileLoaded());
                    break;
                case wxID_UNDO: {
                    const Action* action = actionManager.findMenuAction(wxID_UNDO);
                    assert(action != NULL);
                    if (m_document->canUndoLastCommand()) {
                        event.Enable(true);
                        event.SetText(action->menuItemString(m_document->lastCommandName()));
                    } else {
                        event.Enable(false);
                        event.SetText(action->menuItemString());
                    }
                    break;
                }
                case wxID_REDO: {
                    const Action* action = actionManager.findMenuAction(wxID_REDO);
                    if (m_document->canRedoNextCommand()) {
                        event.Enable(true);
                        event.SetText(action->menuItemString(m_document->nextCommandName()));
                    } else {
                        event.Enable(false);
                        event.SetText(action->menuItemString());
                    }
                    break;
                }
                case CommandIds::Menu::EditRepeat:
                case CommandIds::Menu::EditClearRepeat:
                    event.Enable(true);
                    break;
                case wxID_CUT:
                    event.Enable(m_document->hasSelectedNodes());
                    break;
                case wxID_COPY:
                    event.Enable(m_document->hasSelectedNodes() ||
                                 m_document->hasSelectedBrushFaces());
                    break;
                case wxID_PASTE:
                case CommandIds::Menu::EditPasteAtOriginalPosition: {
                    OpenClipboard openClipboard;
                    event.Enable(wxTheClipboard->IsOpened() && wxTheClipboard->IsSupported(wxDF_TEXT));
                    break;
                }
                case CommandIds::Menu::EditSelectAll:
                    event.Enable(true);
                    break;
                case CommandIds::Menu::EditSelectSiblings:
                    event.Enable(m_document->hasSelectedNodes());
                    break;
                case CommandIds::Menu::EditSelectTouching:
                case CommandIds::Menu::EditSelectInside:
                    event.Enable(m_document->selectedNodes().hasOnlyBrushes());
                    break;
                case CommandIds::Menu::EditSelectByFilePosition:
                    event.Enable(true);
                    break;
                case CommandIds::Menu::EditSelectNone:
                    event.Enable(m_document->hasSelection());
                    break;
                    /*
                case CommandIds::Menu::EditSnapVertices:
                    event.Enable(m_mapView->canSnapVertices());
                    break;
                case CommandIds::Menu::EditReplaceTexture:
                    event.Enable(true);
                    break;
                     */
                case CommandIds::Menu::EditToggleTextureLock:
                    event.Enable(true);
                    event.Check(m_document->textureLock());
                    break;
                case CommandIds::Menu::ViewToggleShowGrid:
                    event.Enable(true);
                    event.Check(m_document->grid().visible());
                    break;
                case CommandIds::Menu::ViewToggleSnapToGrid:
                    event.Enable(true);
                    event.Check(m_document->grid().snap());
                    break;
                case CommandIds::Menu::ViewIncGridSize:
                    event.Enable(m_document->grid().size() < Grid::MaxSize);
                    break;
                case CommandIds::Menu::ViewDecGridSize:
                    event.Enable(m_document->grid().size() > 0);
                    break;
                case CommandIds::Menu::ViewSetGridSize1:
                    event.Enable(true);
                    event.Check(m_document->grid().size() == 0);
                    break;
                case CommandIds::Menu::ViewSetGridSize2:
                    event.Enable(true);
                    event.Check(m_document->grid().size() == 1);
                    break;
                case CommandIds::Menu::ViewSetGridSize4:
                    event.Enable(true);
                    event.Check(m_document->grid().size() == 2);
                    break;
                case CommandIds::Menu::ViewSetGridSize8:
                    event.Enable(true);
                    event.Check(m_document->grid().size() == 3);
                    break;
                case CommandIds::Menu::ViewSetGridSize16:
                    event.Enable(true);
                    event.Check(m_document->grid().size() == 4);
                    break;
                case CommandIds::Menu::ViewSetGridSize32:
                    event.Enable(true);
                    event.Check(m_document->grid().size() == 5);
                    break;
                case CommandIds::Menu::ViewSetGridSize64:
                    event.Enable(true);
                    event.Check(m_document->grid().size() == 6);
                    break;
                case CommandIds::Menu::ViewSetGridSize128:
                    event.Enable(true);
                    event.Check(m_document->grid().size() == 7);
                    break;
                case CommandIds::Menu::ViewSetGridSize256:
                    event.Enable(true);
                    event.Check(m_document->grid().size() == 8);
                    break;
                case CommandIds::Menu::ViewMoveCameraToNextPoint:
                    event.Enable(m_document->isPointFileLoaded() && m_document->pointFile()->hasNextPoint());
                    break;
                case CommandIds::Menu::ViewMoveCameraToPreviousPoint:
                    event.Enable(m_document->isPointFileLoaded() && m_document->pointFile()->hasPreviousPoint());
                    break;
                case CommandIds::Menu::ViewCenterCameraOnSelection:
                    event.Enable(m_document->hasSelectedNodes());
                    break;
                case CommandIds::Menu::ViewMoveCameraToPosition:
                    event.Enable(true);
                    break;
                    /*
                case CommandIds::Menu::ViewSwitchToMapInspector:
                case CommandIds::Menu::ViewSwitchToEntityInspector:
                case CommandIds::Menu::ViewSwitchToFaceInspector:
                    event.Enable(true);
                    break;
                case CommandIds::Menu::FileOpenRecent:
                    event.Enable(true);
                    break;
                 */
                default:
                    if (event.GetId() >= CommandIds::Menu::FileRecentDocuments &&
                        event.GetId() < CommandIds::Menu::FileRecentDocuments + 10)
                        event.Enable(true);
                    else
                        event.Enable(false);
                    break;
            }
        }

        void MapFrame::OnClose(wxCloseEvent& event) {
            if (!IsBeingDeleted()) {
                assert(m_frameManager != NULL);
                if (event.CanVeto() && !confirmOrDiscardChanges())
                    event.Veto();
                else
                    m_frameManager->removeAndDestroyFrame(this);
            }
        }
        
        void MapFrame::OnAutosaveTimer(wxTimerEvent& event) {
            m_autosaver->triggerAutosave(logger());
        }
    }
}
