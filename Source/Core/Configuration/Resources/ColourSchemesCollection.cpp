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
#include "ColourSchemesCollection.h"
#include "SerializationKeys.h"
#include "Config.h"

ColourSchemesCollection::ColourSchemesCollection() :
    ConfigurationResourceCollection(Serialization::Resources::colourSchemes) {}

ColourScheme::Ptr ColourSchemesCollection::getCurrent() const
{
    if (App::Config().containsProperty(Serialization::Config::activeColourScheme))
    {
        ColourScheme::Ptr cs(new ColourScheme());
        App::Config().load(cs.get(), Serialization::Config::activeColourScheme);
        return cs;
    }

    // likely the config file is missing here, meaning the app runs for the first time:
    for (const auto scheme : this->getAll())
    {
        if (scheme->getName().startsWith("Helio Theme v2"))
        {
            return scheme;
        }
    }

    jassertfalse;

    if (const auto firstScheme = this->getAll().getFirst())
    {
        return firstScheme;
    }

    return { new ColourScheme() };
}

void ColourSchemesCollection::setCurrent(const ColourScheme::Ptr scheme)
{
    App::Config().save(scheme.get(), Serialization::Config::activeColourScheme);
}

void ColourSchemesCollection::deserializeResources(const SerializedData &tree, Resources &outResources)
{
    const auto root = tree.hasType(Serialization::Resources::colourSchemes) ?
        tree : tree.getChildWithName(Serialization::Resources::colourSchemes);

    if (!root.isValid()) { return; }

    forEachChildWithType(root, schemeNode, Serialization::UI::Colours::scheme)
    {
        ColourScheme::Ptr scheme(new ColourScheme());
        scheme->deserialize(schemeNode);
        outResources[scheme->getResourceId()] = scheme;
    }
}
