/*
    This file is part of Helio Workstation.

    Helio is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Helio is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Helio. If not, see <http://www.gnu.org/licenses/>.
*/

#include "Common.h"
#include "RootTreeItem.h"

#include "TreeItemChildrenSerializer.h"
#include "ProjectTreeItem.h"
#include "VersionControlTreeItem.h"
#include "PatternEditorTreeItem.h"
#include "TrackGroupTreeItem.h"
#include "PianoTrackTreeItem.h"
#include "AutomationTrackTreeItem.h"
#include "WorkspacePage.h"
#include "MainLayout.h"
#include "DataEncoder.h"
#include "Icons.h"
#include "MidiSequence.h"
#include "AutomationEvent.h"
#include "RecentFilesList.h"
#include "ProjectInfo.h"
#include "WorkspaceMenu.h"
#include "AutomationEvent.h"
#include "AutomationSequence.h"
#include "MidiTrack.h"
#include "App.h"
#include "Workspace.h"


RootTreeItem::RootTreeItem(const String &name) :
    TreeItem(name)
{
    this->setVisible(false);
}

RootTreeItem::~RootTreeItem()
{

}


Colour RootTreeItem::getColour() const
{
    return Colour(0xffffbe92);
}

Image RootTreeItem::getIcon() const
{
    return Icons::findByName(Icons::workspace, TREE_ICON_HEIGHT);
}

void RootTreeItem::showPage()
{
    if (this->introPage == nullptr)
    {
        this->recreatePage();
    }

    App::Layout().showPage(this->introPage, this);
}

void RootTreeItem::recreatePage()
{
    this->introPage = new WorkspacePage(App::Layout());
}

void RootTreeItem::safeRename(const String &newName)
{
    TreeItem::safeRename(newName);
    App::Workspace().getDocument()->renameFile(this->getName());
    this->dispatchChangeTreeItemView();
}

void RootTreeItem::importMidi(File &file)
{
    MidiFile tempFile;
    ScopedPointer<InputStream> in(new FileInputStream(file));
    const bool readOk = tempFile.readFrom(*in);

    if (!readOk)
    {
        DBG("Midi file appears corrupted");
        return;
    }

    if (tempFile.getTimeFormat() <= 0)
    {
        DBG("SMPTE format timing is not yet supported");
        return;
    }

    // важно.
    tempFile.convertTimestampTicksToSeconds();

    ProjectTreeItem *project = new ProjectTreeItem(file.getFileNameWithoutExtension());
    this->addChildTreeItem(project);
    this->addVCS(project);

    for (int trackNum = 0; trackNum < tempFile.getNumTracks(); trackNum++)
    {
        const MidiMessageSequence *currentTrack = tempFile.getTrack(trackNum);
        String trackName = "Track " + String(trackNum);
        MidiTrackTreeItem *layer = this->addPianoTrack(project, trackName);
        layer->importMidi(*currentTrack);
    }

    //this->addAutoLayer(project, "Tempo", 81);

    // todo сохранить по умолчанию рядом - или куда?
    project->broadcastChangeProjectBeatRange();
    project->getDocument()->save();
    App::Workspace().sendChangeMessage();
}


//===----------------------------------------------------------------------===//
// Children
//===----------------------------------------------------------------------===//

void RootTreeItem::checkoutProject(const String &name, const String &id, const String &key)
{
    Array<ProjectTreeItem *> myProjects(this->findChildrenOfType<ProjectTreeItem>());
    
    for (auto myProject : myProjects)
    {
        if (myProject->getId() == id)
        {
            return;
        }
    }
    
    this->setOpen(true);
    auto newProject = new ProjectTreeItem(name);
    this->addChildTreeItem(newProject);
    
    auto vcs = new VersionControlTreeItem(id, key);
    newProject->addChildTreeItem(vcs);
    
    vcs->asyncPullAndCheckoutOrDeleteIfFailed();
    App::Workspace().sendChangeMessage();
}

ProjectTreeItem *RootTreeItem::openProject(const File &file, int insertIndex /*= -1 */)
{
    Array<ProjectTreeItem *> myProjects(this->findChildrenOfType<ProjectTreeItem>());

    // под десктопом первым элементов всегда должны быть инструменты
#if HELIO_DESKTOP
    const int insertIndexCorrection = (insertIndex == 0) ? 1 : insertIndex;
#elif HELIO_MOBILE
    const int insertIndexCorrection = insertIndex;
#endif

    // предварительная проверка на дубликаты - по полному пути
    for (auto myProject : myProjects)
    {
        if (myProject->getDocument()->getFullPath() == file.getFullPathName())
        {
            return nullptr;
        }
    }

    Logger::writeToLog("Opening: " + file.getFullPathName());
    
    if (file.existsAsFile())
    {
        auto project = new ProjectTreeItem(file);
        this->addChildTreeItem(project, insertIndexCorrection);

        if (!project->getDocument()->load(file.getFullPathName()))
        {
            App::Workspace().getRecentFilesList().removeByPath(file.getFullPathName());
            delete project;
            return nullptr;
        }

        // вторая проверка на дубликаты - по id
        for (auto myProject : myProjects)
        {
            if (myProject->getId() == project->getId())
            {
                App::Workspace().getRecentFilesList().removeByPath(file.getFullPathName());
                delete project;
                return nullptr;
            }
        }

        App::Workspace().sendChangeMessage();
        return project;
    }

    return nullptr;
}

ProjectTreeItem *RootTreeItem::addDefaultProject(const String &projectName)
{
    this->setOpen(true);
    auto newProject = new ProjectTreeItem(projectName);
    this->addChildTreeItem(newProject);
    return this->createDefaultProjectChildren(newProject);
}

ProjectTreeItem *RootTreeItem::addDefaultProject(const File &projectLocation)
{
    this->setOpen(true);
    auto newProject = new ProjectTreeItem(projectLocation);
    this->addChildTreeItem(newProject);
    return this->createDefaultProjectChildren(newProject);
}

ProjectTreeItem *RootTreeItem::createDefaultProjectChildren(ProjectTreeItem *newProject)
{
    VersionControlTreeItem *vcs = this->addVCS(newProject);
    newProject->addChildTreeItem(new PatternEditorTreeItem());

    this->addPianoTrack(newProject, "Arps")->setTrackColour(Colours::orangered);
    this->addPianoTrack(newProject, "Counterpoint")->setTrackColour(Colours::gold);
    this->addPianoTrack(newProject, "Melodic")->setTrackColour(Colours::chartreuse);
    this->addAutoLayer(newProject, "Tempo", MidiTrack::tempoController)->setTrackColour(Colours::floralwhite);

    newProject->getDocument()->save();
    newProject->broadcastChangeProjectBeatRange();
    
    // notify recent files list
    App::Workspace().getRecentFilesList().
    onProjectStateChanged(newProject->getName(),
                          newProject->getDocument()->getFullPath(),
                          newProject->getId(),
                          true);
    return newProject;
}

VersionControlTreeItem *RootTreeItem::addVCS(TreeItem *parent)
{
    auto vcs = new VersionControlTreeItem();
    parent->addChildTreeItem(vcs);

    // при создании рутовой ноды vcs, туда надо первым делом коммитить пустой ProjectInfo,
    // чтобы оной в списке изменений всегда показывался как измененный (не добавленный)
    // т.к. удалить его нельзя. и смущать юзера подобными надписями тоже не айс.
    vcs->commitProjectInfo();

    return vcs;
}

TrackGroupTreeItem *RootTreeItem::addGroup(TreeItem *parent, const String &name)
{
    auto group = new TrackGroupTreeItem(name);
    parent->addChildTreeItem(group);
    return group;
}

MidiTrackTreeItem *RootTreeItem::addPianoTrack(TreeItem *parent, const String &name)
{
    MidiTrackTreeItem *item = new PianoTrackTreeItem(name);
    parent->addChildTreeItem(item);
    return item;
}

MidiTrackTreeItem *RootTreeItem::addAutoLayer(TreeItem *parent, const String &name, int controllerNumber)
{
    MidiTrackTreeItem *item = new AutomationTrackTreeItem(name);
    item->setTrackControllerNumber(controllerNumber);
    AutomationSequence *itemLayer = static_cast<AutomationSequence *>(item->getSequence());
    parent->addChildTreeItem(item);
    itemLayer->insert(AutomationEvent(itemLayer, 0, 0.5), false);
    return item;
}


//===----------------------------------------------------------------------===//
// Menu
//===----------------------------------------------------------------------===//

ScopedPointer<Component> RootTreeItem::createItemMenu()
{
    return new WorkspaceMenu(&App::Workspace());
}


//===----------------------------------------------------------------------===//
// Dragging
//===----------------------------------------------------------------------===//

bool RootTreeItem::isInterestedInDragSource(const DragAndDropTarget::SourceDetails &dragSourceDetails)
{
    //if (TreeView *treeView = dynamic_cast<TreeView *>(dragSourceDetails.sourceComponent.get()))
    //{
    //    TreeItem *selected = TreeItem::getSelectedItem(treeView);

    //if (TreeItem::isNodeInChildren(selected, this))
    //{ return false; }

    return (dragSourceDetails.description == Serialization::Core::project);
    //}
}

bool RootTreeItem::isInterestedInFileDrag(const StringArray &files)
{
    return File::createFileWithoutCheckingPath(files[0])
           .hasFileExtension("hp;helioproject;helio");
}

void RootTreeItem::filesDropped(const StringArray &files, int insertIndex)
{
    for (const auto & i : files)
    {
        const File file(i);
        this->openProject(file, insertIndex);
    }
}


//===----------------------------------------------------------------------===//
// Serializable
//===----------------------------------------------------------------------===//

XmlElement *RootTreeItem::serialize() const
{
    auto xml = new XmlElement(Serialization::Core::treeItem);
    xml->setAttribute("type", Serialization::Core::root);
    xml->setAttribute("name", this->name);

    TreeItemChildrenSerializer::serializeChildren(*this, *xml);

    return xml;
}

void RootTreeItem::deserialize(const XmlElement &xml)
{
    this->reset();

    const XmlElement *root = xml.hasTagName(Serialization::Core::treeItem) ?
        &xml : xml.getChildByName(Serialization::Core::treeItem);

    if (root == nullptr) { return; }

    const String type = root->getStringAttribute("type");

    if (type != Serialization::Core::root) { return; }

    this->name = root->getStringAttribute("name", this->name);

    TreeItemChildrenSerializer::deserializeChildren(*this, *root);
}
