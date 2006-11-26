/*MT*
    
    MediaTomb - http://www.mediatomb.org/
    
    config_manager.cc - this file is part of MediaTomb.
    
    Copyright (C) 2005 Gena Batyan <bgeradz@mediatomb.org>,
                       Sergey Bostandzhyan <jin@mediatomb.org>
    Copyright (C) 2006 Gena Batyan <bgeradz@mediatomb.org>,
                       Sergey Bostandzhyan <jin@mediatomb.org>,
                       Leonhard Wimmer <leo@mediatomb.org>
    
    MediaTomb is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
    
    MediaTomb is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    
    You should have received a copy of the GNU General Public License
    along with MediaTomb; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
    
    $Id$
*/

/// \file config_manager.cc

#ifdef HAVE_CONFIG_H
    #include "autoconfig.h"
#endif

#include <stdio.h>
#include "uuid/uuid.h"
#include "common.h"
#include "config_manager.h"
#include "storage.h"
#include <sys/types.h>
#include <sys/stat.h>
#include "tools.h"

#ifdef HAVE_LANGINFO_H
    #include <langinfo.h>
#endif

using namespace zmm;
using namespace mxml;

static Ref<ConfigManager> instance;

String ConfigManager::construct_path(String path)
{
    String home = getOption(_("/server/home"));
#ifndef __CYGWIN__
    if (path.charAt(0) == '/')
        return path;
#else
    if (path.charAt(1) == ':')
        return path;
#endif
    if (home == "." && path.charAt(0) == '.')
        return path;
    
    if (home == "")
        return _(".") + DIR_SEPARATOR + path;
    else
        return home + DIR_SEPARATOR + path;
}

ConfigManager::ConfigManager() : Object()
{
}

String ConfigManager::createDefaultConfig(String userhome)
{
    bool mysql_flag = false;

    String homepath = userhome + DIR_SEPARATOR + DEFAULT_CONFIG_HOME;

    if (!check_path(homepath, true))
    {
        if (mkdir(homepath.c_str(), 0777) < 0)
        {
            throw _Exception(_("Could not create directory ") + homepath + 
                    " : " + strerror(errno));
        }
    }

    String config_filename = homepath + DIR_SEPARATOR + DEFAULT_CONFIG_NAME;

    Ref<Element> config(new Element(_("config")));
    Ref<Element> server(new Element(_("server")));
    
    Ref<Element> ui(new Element(_("ui")));
    ui->addAttribute(_("enabled"), _(DEFAULT_UI_EN_VALUE));

    Ref<Element>accounts(new Element(_("accounts")));
    accounts->addAttribute(_("enabled"), _(DEFAULT_ACCOUNTS_EN_VALUE));
    accounts->addAttribute(_("session-timeout"), String::from(DEFAULT_SESSION_TIMEOUT));
    ui->appendChild(accounts);
    
    server->appendChild(ui);
    server->appendTextChild(_("name"), _(PACKAGE_NAME));
    
    Ref<Element> udn(new Element(_("udn")));
    server->appendChild(udn);

    server->appendTextChild(_("home"), homepath);
    server->appendTextChild(_("webroot"), String(_(PACKAGE_DATADIR)) + 
                                                 DIR_SEPARATOR +  
                                                 _(DEFAULT_WEB_DIR));
    
    Ref<Element> storage(new Element(_("storage")));
    storage->addAttribute(_("driver"), _(DEFAULT_STORAGE_DRIVER));
#ifdef HAVE_SQLITE3
    storage->appendTextChild(_("database-file"), _(DEFAULT_SQLITE3_DB_FILENAME));
#else
    storage->appendTextChild(_("host"), _(DEFAULT_MYSQL_HOST));
    storage->appendTextChild(_("username"), _(DEFAULT_MYSQL_USER));
//    storage->appendTextChild(_("password"), _(DEFAULT_MYSQL_PASSWORD));
    storage->appendTextChild(_("database"), _(DEFAULT_MYSQL_DB));
    mysql_flag = true;
#endif
    server->appendChild(storage);
    config->appendChild(server);

    Ref<Element> import(new Element(_("import")));
    import->addAttribute(_("hidden-files"), _(DEFAULT_HIDDEN_FILES_VALUE));
    import->appendTextChild(_("script"), String(_(PACKAGE_DATADIR)) +
                                                DIR_SEPARATOR + 
                                                _(DEFAULT_JS_DIR) +
                                                DIR_SEPARATOR +
                                                _(DEFAULT_IMPORT_SCRIPT));


    String map_file = _(PACKAGE_DATADIR) + DIR_SEPARATOR + CONFIG_MAPPINGS_TEMPLATE;

    try
    {
        Ref<Parser> parser(new Parser());
        Ref<Element> mappings(new Element(_("mappings")));
        mappings = parser->parseFile(map_file);
        import->appendChild(mappings);
    }
    catch (ParseException pe)
    {
        log_error("Error parsing template file: %s line %d:\n%s\n",
                pe.context->location.c_str(),
                pe.context->line,
                pe.getMessage().c_str());
        exit(EXIT_FAILURE);
    }

    catch (Exception ex)
    {
        log_error("Could not import mapping template file from %s\n",
                map_file.c_str());
    }
    config->appendChild(import);

    save_text(config_filename, config->print());
    log_info("MediaTomb configuration was created in: %s\n", 
            config_filename.c_str());

    if (mysql_flag)
    {
        throw _Exception(_("You are using MySQL! Please edit ") + config_filename +
                " and enter your MySQL host/username/password!");
    }

    return config_filename;
}

void ConfigManager::init(String filename, String userhome)
{
    if (instance == nil)
        instance = Ref<ConfigManager>(new ConfigManager());

    String error_message;
    String home;
    bool home_ok = true;

    if (filename == nil)
    {
        // we are looking for ~/.mediatomb
        if (home_ok && (!check_path(userhome + DIR_SEPARATOR + DEFAULT_CONFIG_HOME + DIR_SEPARATOR + DEFAULT_CONFIG_NAME)))
        {
            home_ok = false;
        }
        else
        {
            home = userhome + DIR_SEPARATOR + DEFAULT_CONFIG_HOME;
            filename = home + DIR_SEPARATOR + DEFAULT_CONFIG_NAME;
        }

        if ((!home_ok) && (string_ok(userhome)))
        {
            filename = instance->createDefaultConfig(userhome);
        }
   
    }

    if (filename == nil)
    {       
        throw _Exception(_("\nThe server configuration file could not be found in ~/.mediatomb\n") +
                "MediaTomb could not determine your home directory - automatic setup failed.\n" + 
                "Try specifying an alternative configuration file on the command line.\n" + 
                "For a list of options run: mediatomb -h\n");
    }

    log_info("Loading configuration from: %s\n", filename.c_str());
    instance->load(filename);

    instance->prepare_udn();    
    instance->validate(home);    
}

void ConfigManager::validate(String serverhome)
{
    String temp;

    log_info("Checking configuration...\n");
   
    // first check if the config file itself looks ok, it must have a config
    // and a server tag
    if (root->getName() != "config")
        throw _Exception(_("Error in config file: <config> tag not found"));

    if (root->getChild(_("server")) == nil)
        throw _Exception(_("Error in config file: <server> tag not found"));

    // now go through the mandatory parameters, if something is missing
    // here we will not start the server
//    temp = checkOption_("/server/home");
//    check_path_ex(temp, true);

    getOption(_("/server/home"), serverhome); 

    prepare_path(_("/server/home"), true);
    
//    temp = checkOption_("/server/webroot");
//    check_path_ex(construct_path(temp), true);

    prepare_path(_("/server/webroot"), true);
    
    if (string_ok(getOption(_("/server/servedir"), _(""))))
        prepare_path(_("/server/servedir"), true);
    
    // udn should be already prepared
    checkOptionString(_("/server/udn"));

    checkOptionString(_("/server/storage/attribute::driver"));

    String dbDriver = getOption(_("/server/storage/attribute::driver"));

    // checking database driver options
    do
    {
#ifdef HAVE_SQLITE3
        if (dbDriver == "sqlite3")
        {
            prepare_path(_("/server/storage/database-file"), false, true);
            break;
        }
#endif

#ifdef HAVE_MYSQL
        if (dbDriver == "mysql")
        {
            getOption(_("/server/storage/host"), _(DEFAULT_MYSQL_HOST));
            getOption(_("/server/storage/database"), _(DEFAULT_MYSQL_DB));
            getOption(_("/server/storage/username"), _(DEFAULT_MYSQL_USER));
            getOption(_("/server/storage/port"), _("0"));
            //getOption(_("/server/storage/socket")); - checked in mysql_storage.cc
            //getOption(_("/server/storage/password")); - checked in mysql_storage.cc
            break;
        }
#endif
        // other database types...
        throw _Exception(_("Unknown storage driver: ") + dbDriver);
    }
    while (false);

    
//    temp = checkOption_("/server/storage/database-file");
//    check_path_ex(construct_path(temp));

    // now go through the optional settings and fix them if anything is missing
   
    temp = getOption(_("/server/ui/attribute::enabled"),
                     _(DEFAULT_UI_EN_VALUE));
    if ((temp != "yes") && (temp != "no"))
        throw _Exception(_("Error in config file: incorrect parameter for <ui enabled=\")\" /> attribute"));

    temp = getOption(_("/server/ui/accounts/attribute::enabled"), 
            _(DEFAULT_ACCOUNTS_EN_VALUE));

    if ((temp != "yes") && (temp != "no"))
        throw _Exception(_("Error in config file: incorrect parameter for <accounts enabled=\")\" /> attribute"));

    int i  = getIntOption(_("/server/ui/accounts/attribute::session-timeout"), DEFAULT_SESSION_TIMEOUT);
    if (i < 1)
    {
        throw _Exception(_("Error in config file: invalid session-timeout %d (must be > 0)\n"));
    }

    temp = getOption(_("/import/attribute::hidden-files"),
                     _(DEFAULT_HIDDEN_FILES_VALUE));
    if ((temp != "yes") && (temp != "no"))
        throw _Exception(_("Error in config file: incorrect parameter for <import hidden-files=\")\" /> attribute"));

    getOption(_("/import/mappings/extension-mimetype/attribute::ignore-unknown"),
              _(DEFAULT_IGNORE_UNKNOWN_EXTENSIONS));

#ifdef HAVE_NL_LANGINFO
    temp = String(nl_langinfo(CODESET));
    log_debug("received %s from nl_langinfo\n", temp.c_str());
    if (!string_ok(temp))
        temp = _(DEFAULT_FILESYSTEM_CHARSET);
#else
    temp = _(DEFAULT_FILESYSTEM_CHARSET);
#endif      
    String charset = getOption(_("/import/filesystem-charset"), temp);
    log_info("Setting filesystem import charset to %s\n", charset.c_str());
    charset = getOption(_("/import/metadata-charset"), temp);
    log_info("Setting metadata import charset to %s\n", charset.c_str());

    getOption(_("/server/ip"), _("")); // bind to any IP address
    getOption(_("/server/bookmark"), _(DEFAULT_BOOKMARK_FILE));
    getOption(_("/server/name"), _(DESC_FRIENDLY_NAME));
    getOption(_("/server/model"), _(DESC_MODEL_NAME));

/*
#ifdef HAVE_JS
    try
    {
        temp = getOption(_("/import/script"));
    }
    catch (Exception ex)
    {
        temp = "";
    }

    if (string_ok(temp))
            check_path_ex(construct_path(temp));

    Ref<Element> script = getElement("/import/script");
    if (script != nil)
            script->setText(construct_path(temp));
#endif
*/

#ifdef HAVE_JS
    prepare_path(_("/import/script"));
#endif

    getIntOption(_("/server/port"), 0); // 0 means, that the SDK will any free port itself
    getIntOption(_("/server/alive"), DEFAULT_ALIVE_INTERVAL);

    Ref<Element> el = getElement(_("/import/mappings/mimetype-upnpclass"));
    if (el == nil)
    {
//        Ref<Dictionary> dict = createDictionaryFromNodeset(el, "map", "from", "to");
        getOption(_("/import/mappings/mimetype-upnpclass"), _(""));
    }
  
    el = getElement(_("/import/autoscan"));
    if (el == nil)
    {
        getOption(_("/import/autoscan"), _(""));
    }
      
    el = getElement(_("/server/custom-http-headers"));
    if (el == nil)
    {
        getOption(_("/server/custom-http-headers"), _(""));
    }

#ifdef HAVE_EXIF    

    el = getElement(_("/import/library-options/libexif/auxdata"));
    if (el == nil)
    {
    //    Ref<Array<StringBase> > arr = createArrayFromNodeset(el, "add", "tag");
        getOption(_("/import/library-options/libexif/auxdata"),
                  _(""));
        
    }

#endif // HAVE_EXIF

#ifdef HAVE_EXTRACTOR

    el = getElement(_("/import/library-options/libextractor/auxdata"));
    if (el == nil)
    {
    //    Ref<Array<StringBase> > arr = createArrayFromNodeset(el, "add", "tag");
        getOption(_("/import/library-options/libextractor/auxdata"),
                  _(""));
    }
#endif // HAVE_EXTRACTOR

#ifdef HAVE_MAGIC
    if (string_ok(getOption(_("/import/magic-file"), _(""))))
    {
        prepare_path(_("/import/magic-file"));
    }
#endif

    log_info("Configuration check succeeded.\n");

    log_debug("Config file dump after validation: \n%s\n", root->print().c_str());
}

void ConfigManager::prepare_udn()
{
    bool need_to_save = false;
 
    if (root->getName() != "config")
        return;

    Ref<Element> server = root->getChild(_("server"));
    if (server == nil)
        return;

    Ref<Element> element = server->getChild(_("udn"));
    if (element == nil || element->getText() == nil || element->getText() == "")
    {
        char   uuid_str[37];
        uuid_t uuid;
                
        uuid_generate(uuid);
        uuid_unparse(uuid, uuid_str);

        log_info("UUID generated: %s\n", uuid_str);
       
        getOption(_("/server/udn"), String("uuid:") + uuid_str);

        need_to_save = true;
    }
    
    if (need_to_save)
        save();
}

void ConfigManager::prepare_path(String xpath, bool needDir, bool existenceUnneeded)
{
    String temp;

    temp = checkOptionString(xpath);
    
    temp = construct_path(temp);

    check_path_ex(temp, needDir, existenceUnneeded);

    Ref<Element> script = getElement(xpath);
    if (script != nil)
        script->setText(temp);
}

Ref<ConfigManager> ConfigManager::getInstance()
{
    return instance;
}


void ConfigManager::save()
{
    save_text(filename, root->print());
}

void ConfigManager::save_text(String filename, String content)
{
    FILE *file = fopen(filename.c_str(), "wb");
    if (file == NULL)
    {
        throw _Exception(_("could not open file ") +
                        filename + " for writing : " + strerror(errno));
    }

    size_t bytesWritten = fwrite(content.c_str(), sizeof(char),
                              content.length(), file);
    if (bytesWritten < (size_t)content.length())
    {
        throw _Exception(_("could not write to config file ") +
                        filename + " : " + strerror(errno));
    }
    
    fclose(file);
}

void ConfigManager::load(String filename)
{
    this->filename = filename;
    Ref<Parser> parser(new Parser());
    root = parser->parseFile(filename);
    if (root == nil)
    {
        throw _Exception(_("Unable to parse server configuration!"));
    }
}

String ConfigManager::getOption(String xpath, String def)
{      
    Ref<XPath> rootXPath(new XPath(root));
    String value = rootXPath->getText(xpath);
    if (string_ok(value))
        return trim_string(value);

    log_info("Config: option not found: %s using default value: %s\n",
           xpath.c_str(), def.c_str());
    
    String pathPart = XPath::getPathPart(xpath);
    String axisPart = XPath::getAxisPart(xpath);

    Ref<Array<StringBase> >parts = split_string(pathPart, '/');
    
    Ref<Element> cur = root;
    String attr = nil;
    
    int i;
    Ref<Element> child;
    for (i = 0; i < parts->size(); i++)
    {
        String part = parts->get(i);
        child = cur->getChild(part);
        if (child == nil)
            break;
        cur = child;
    }
    // here cur is the last existing element in the path
    for (; i < parts->size(); i++)
    {
        String part = parts->get(i);
        child = Ref<Element>(new Element(part));
        cur->appendChild(child);
        cur = child;
    }
    
    if (axisPart != nil)
    {
        String axis = XPath::getAxis(axisPart);
        String spec = XPath::getSpec(axisPart);
        if (axis != "attribute")
        {
            throw _Exception(_("ConfigManager::getOption: only attribute:: axis supported"));
        }
        cur->setAttribute(spec, def);
    } 
    else
        cur->setText(def);

    return def;
}

int ConfigManager::getIntOption(String xpath, int def)
{
    String sDef;

    sDef = String::from(def);
    
    String sVal = getOption(xpath, sDef);
    return sVal.toInt();
}

String ConfigManager::getOption(String xpath)
{      
    Ref<XPath> rootXPath(new XPath(root));
    String value = rootXPath->getText(xpath);

    /// \todo is this ok?
//    if (string_ok(value))
//        return value;
    if (value != nil)
        return trim_string(value);
    throw _Exception(_("Config: option not found: ") + xpath);
}

int ConfigManager::getIntOption(String xpath)
{
    String sVal = getOption(xpath);
    int val = sVal.toInt();
    return val;
}


Ref<Element> ConfigManager::getElement(String xpath)
{      
    Ref<XPath> rootXPath(new XPath(root));
    return rootXPath->getElement(xpath);
}

void ConfigManager::writeBookmark(String ip, String port)
{
    FILE    *f;
    String  filename;
    String  path;
    String  data; 
    int     size; 
  
    String value = ConfigManager::getInstance()->getOption(_("/server/ui/attribute::enabled"));
    if (value != "yes")
    {
        data = http_redirect_to(ip, port, _("disabled.html"));
    }
    else
    {
        data = http_redirect_to(ip, port);
    }

    filename = getOption(_("/server/bookmark"));
    path = construct_path(filename);
    
        
    f = fopen(path.c_str(), "w");
    if (f == NULL)
    {
        throw _Exception(_("writeBookmark: failed to open: ") + path);
    }

    size = fwrite(data.c_str(), sizeof(char), data.length(), f);
    fclose(f);

    if (size < data.length())
        throw _Exception(_("write_Bookmark: failed to write to: ") + path);

}

String ConfigManager::checkOptionString(String xpath)
{
    String temp = getOption(xpath);
    if (!string_ok(temp))
        throw _Exception(_("Config: value of ") + xpath + " tag is invalid");

    return temp;
}

Ref<Dictionary> ConfigManager::createDictionaryFromNodeset(Ref<Element> element, String nodeName, String keyAttr, String valAttr)
{
    Ref<Dictionary> dict(new Dictionary());
    String key;
    String value;

    for (int i = 0; i < element->childCount(); i++)
    {
        Ref<Element> child = element->getChild(i);
        if (child->getName() == nodeName)
        {
            key = child->getAttribute(keyAttr);
            value = child->getAttribute(valAttr);

            if (string_ok(key) && string_ok(value))
                dict->put(key, value);
        }
        
    }

    return dict;
}

Ref<AutoscanList> ConfigManager::createAutoscanListFromNodeset(zmm::Ref<mxml::Element> element, scan_mode_t scanmode)
{
    Ref<AutoscanList> list(new AutoscanList());
    String location;
    String temp;
    scan_level_t level;
    scan_mode_t mode;
    bool recursive;
    bool hidden;
    unsigned int interval;
   
    for (int i = 0; i < element->childCount(); i++)
    {
        hidden = false;
        recursive = false;

        Ref<Element> child = element->getChild(i);
        if (child->getName() == "directory")
        {
            location = child->getAttribute(_("location"));
            if (!string_ok(location))
            {
                log_warning("Skipping autoscan directory with invalid location!\n");
                continue;
            }

            if (check_path(location, false))
            {
                log_warning("Skipping %s - not a directory!\n", location.c_str());
                continue;
            }
            
            temp = child->getAttribute(_("mode"));
            if (!string_ok(temp) || (temp != "timed"))
            {
                log_warning("Skipping autoscan directory %s: mode attribute is missing or invalid\n", location.c_str());
                continue;
            }
            else
            {
                mode = TimedScanMode;
            }

            if (mode != scanmode)
                continue; // skip scan modes that we are not interested in (content manager needs one mode type per array)
            
            temp =  child->getAttribute(_("level"));
            if (!string_ok(temp))
            {
                log_warning("Skipping autoscan directory %s: level attribute is missing or invalid\n", location.c_str());
                continue;
            }
            else
            {
                if (temp == "basic")
                    level = BasicScanLevel;
                else if (temp == "full")
                    level = FullScanLevel;
                else
                {
                    log_warning("Skipping autoscan directory %s: level attribute %s is invalid\n", location.c_str(), temp.c_str());
                    continue;
                }
            }

            temp = child->getAttribute(_("recursive"));
            if (!string_ok(temp))
            {
                log_warning("Skipping autoscan directory %s: recursive attribute is missing or invalid\n", location.c_str());
                continue;
            }
            else
            {
               if (temp == "yes")
                   recursive = true;
               else if (temp == "no")
                   recursive = false;
               else
               {
                   log_warning("Skipping autoscan directory %s: recusrive attribute %s is invalid\n", location.c_str(), temp.c_str());
                   continue;
               }
            }

            temp = child->getAttribute(_("hidden-files"));
            if (!string_ok(temp))
            {
                temp = getOption(_("/import/attribute::hidden-files"));
            }

            if (temp == "yes")
                hidden = true;
            else if (temp == "no")
                hidden = false;
            else
            {
                log_warning("Skipping autoscan directory %s: hidden attribute %s is invalid\n", location.c_str(), temp.c_str());
                continue;
            }

            temp = child->getAttribute(_("interval"));
            interval = 0;
            if (mode == TimedScanMode)
            {
                if (!string_ok(temp))
                {
                    log_warning("Skipping autoscan directory %s: interval attribute is required for timed mode\n", location.c_str());
                    continue;
                }

                interval = temp.toUInt();

                if (interval == 0)
                {
                    log_warning("Skipping autoscan directory %s: invalid interval attribute\n", location.c_str());
                    continue;
                }
            }
            
            Ref<AutoscanDirectory> dir(new AutoscanDirectory(location, mode, level, recursive, true, -1, interval, hidden));
            list->add(dir); 
        }
    }

    return list;
}


Ref<Array<StringBase> > ConfigManager::createArrayFromNodeset(Ref<mxml::Element> element, String nodeName, String attrName)
{
    String attrValue;
    Ref<Array<StringBase> > arr(new Array<StringBase>());

    for (int i = 0; i < element->childCount(); i++)
    {
        Ref<Element> child = element->getChild(i);
        if (child->getName() == nodeName)
        {
            attrValue = child->getAttribute(attrName);

            if (string_ok(attrValue))
                arr->append(attrValue);
        }
    }

    return arr;
}
