#include "myfs.hpp"
#include <cmath>
#include <iostream>
#include <iomanip>
#include <list>
#include <memory>
#include <sstream>  //istringstream
#include <string>
#include <vector>
#include <deque>
#include <assert.h>
//#include "../include/direntry.hpp"
//#include "../include/inode.hpp"
//#include "../include/freenode.hpp"

using namespace std;

// less than opt required
#define ops_at_least(x)                                 \
  if (static_cast<int>(args.size()) < x+1) {            \
    cerr << args[0] << ": missing operand" << endl;     \
    return;                                             \
  }

// more than opt required
#define ops_less_than(x)                                \
  if (static_cast<int>(args.size()) > x+1) {            \
    cerr << args[0] << ": too many operands" << endl;   \
    return;                                             \
  }\


// check exact opt arg nums
#define ops_exactly(x)                          \
  ops_at_least(x);                              \
  ops_less_than(x);

//// Constructor
//myFS::myFS(const string& filename,
//           const uint fs_size,
//           const uint block_size,
//           const uint direct_blocks):
//        filename(filename),
//        block_size(block_size),
//        direct_blocks(direct_blocks),
//        block_num(ceil(static_cast<double>(fs_size) / block_size))
//{
//    // init inode
//    Inode::block_size = block_size;
//    Inode::free_list = &free_list;
//
//
//    root_dir = DirEntry::make_de_dir("root", nullptr);
//
//    // start at root dir/ set pwd
//    pwd = root_dir;
//
//    // init disk
//    init_disk(filename);
//    free_list.emplace_back(block_num, 0);
//}

// Constructor
myFS::myFS(string& filename)
{
    block_num=BLOCK_NUM;

    // init inode
    Inode::block_size = block_size;
    Inode::free_list = &free_list;

    class SuperBlock *p_sb=new SuperBlock();
    p_sb->myfs=this;
    p_sb->write_back_to_disk();

    root_dir = DirEntry::make_de_dir("root", nullptr); // make dir de

    // start at root dir/ set pwd
    pwd = root_dir;

    // init disk
    init_disk(filename);
    free_list.emplace_back(block_num, 0); // make freenode
}

myFS::~myFS() 
{
    disk_file.close();
    remove(filename.c_str());
}


void myFS::init_disk(const string& filename)
{
    // write disk with 0, prevent some dirty data
    const vector<char>zeroes(block_num, 0);

    disk_file.open(filename,
                    fstream::in |
                    fstream::out |
                    fstream::binary |
                    fstream::trunc);

    for (uint i = 0; i < block_num; ++i) 
    {
        disk_file.write(zeroes.data(), block_size);
    }
}

//D: parase path Layer by layer to the last node
//I: path string
//O: pathret ptr
unique_ptr<myFS::PathRet> myFS::parse_path(string path_str) const 
{
    unique_ptr<PathRet> ret(new PathRet);

    // check if path is relative or absolute
    ret->final_node = pwd;

    // pwd==root
    if (path_str[0] =='/') 
    {
        path_str.erase(0,1);
        ret->final_node = root_dir;
    }

    // initialize data structure
    ret->final_name = ret->final_node->name;
    ret->parent_node = ret->final_node->parent.lock();

    // tokenize the string, redirector
    istringstream is(path_str);
    string token;
    vector<string> path_tokens;
    while (getline(is, token, '/')) 
    {
        path_tokens.push_back(token);
    }

    // walk the path updating pointers
    for (auto &node_name : path_tokens) 
    {
        // something other than the last entry was not found
        if (ret->final_node == nullptr) 
        {
            ret->invalid_path = true;
            return ret;
        }
        ret->parent_node = ret->final_node;
        ret->final_node = ret->final_node->find_child(node_name);
        ret->final_name = node_name;
    }

    return ret;
}

//TODO: change this
bool myFS::getMode(Mode *mode, string mode_s)
{
    if (mode_s == "w") 
    {
        *mode = W;
    } 
    else if(mode_s == "r") 
    {
    *mode = R;
    } 
    else if (mode_s == "rw") 
    {
    *mode = RW;
    }
    else 
    {
        return false;
    }
    return true;
}


bool myFS::basic_open(Descriptor *d, vector <string> args) 
{
    assert(args.size() == 3);

    Mode mode;
    auto path = parse_path(args[1]);            //path ret
    auto node = path->final_node;               //final node
    auto parent = path->parent_node;            //parent node
    bool known_mode = getMode(&mode, args[2]);

    if (path->invalid_path == true)
    {
        cerr << args[0] << ": error: Invalid path: " << args[1] << endl;
    } 
    else if(!known_mode)
    {
        cerr << args[0] << ": error: Unknown mode: " << args[2] << endl;
    } 
    else if (node == nullptr && (mode == R || mode == RW)) 
    {
        cerr << args[0] << ": error: " << args[1] << " does not exist." << endl;
    } 
    else if (node != nullptr && node->type == dir) 
    {
        cerr << args[0] << ": error: Cannot open a directory." << endl;
    } 
    else if (node != nullptr && node->is_locked) 
    {
        cerr << args[0] << ": error: " << args[1] << " is already open." << endl;
    } 
    else 
    {
        //create the file if the file not exist
        if(node == nullptr) 
        {
            node = parent->add_file(path->final_name);
        }
        // get a descriptor
        uint fd = next_descriptor++;
        node->is_locked = true;
        *d = Descriptor{mode, 0, node->inode, node, fd};
        open_files[fd] = *d;        //save open file descriptor
        return true;
    }
    return false;
}

// wrap basic open
void myFS::open(vector<string> args) 
{
    ops_exactly(2);
    Descriptor desc;
    if (basic_open(&desc, args)) 
    {
        cout << "SUCCESS: fd=" << desc.fd << endl;
    }
}

//D: wrap basic_read safely, read a file
//I: read filename
//O: S/F
void myFS::read(vector<string> args)
{
    ops_exactly(2);

    uint fd;
    if ( !(istringstream(args[1]) >> fd)) 
    {
        cerr << "read: error: Unknown descriptor." << endl;
        return;
    }
    auto desc_it = open_files.find(fd);
    if (desc_it == open_files.end())    // last is empty
    {
        cerr << "read: error: File descriptor not open." << endl;
        return;
    }
    auto &desc = desc_it->second; //map value
    if(desc.mode != R && desc.mode != RW) 
    {
        cerr << "read: error: " << args[1] << " not open for read." << endl;
        return;
    }

    uint size;
    if (!(istringstream(args[2]) >> size)) 
    {
        cerr << "read: error: Invalid read size." << endl;
    } 
    else if (size + desc.byte_pos > desc.inode.lock()->size) // Out of read ava zone
    {
        cerr << "read: error: Read goes beyond file end." << endl;
    } 
    else 
    {
        auto data = basic_read(desc, size);
        cout << *data << endl;
    }
}

//I: desc, read size
//O: dara ptr
unique_ptr<string> myFS::basic_read(Descriptor &desc, const uint size) 
{
    //for constrite the size, using char instead of string
    char *data = new char[size];
    char *data_p = data;
    uint &pos = desc.byte_pos;
    uint bytes_to_read = size;
    auto inode = desc.inode.lock();

    uint dbytes = direct_blocks * block_size;   //
    while (bytes_to_read > 0) 
    {
        uint read_size = min(bytes_to_read, block_size - pos % block_size); // prevent reading out of range
        uint read_src;
        if (pos < dbytes) 
        {
            read_src = inode->d_blocks[pos / block_size] + pos % block_size;
        } 
        else 
        {
            uint i = (pos - dbytes) / (direct_blocks * block_size);
            uint j = (pos - dbytes) / block_size % direct_blocks;
            read_src = inode->i_blocks->at(i)[j] + pos % block_size;
        }
        disk_file.seekp(read_src);
        disk_file.read(data_p, read_size);
        pos += read_size;
        data_p += read_size;
        bytes_to_read -= read_size;
    }
    return unique_ptr<string>(new string(data, size));
}

//D: wrap write safely
//I: write filename
//O: S/F
void myFS::write(vector<string> args) 
{
    ops_exactly(2);

    uint fd;
    uint max_size = block_size * (direct_blocks + direct_blocks * direct_blocks);
    if ( !(istringstream(args[1]) >> fd))   // desc error
    {
        cerr << "write: error: Unknown descriptor." << endl;
    } 
    else 
    {
        auto desc = open_files.find(fd);
        if (desc == open_files.end())   //final not open
        {
            cerr << "write: error: File descriptor not open." << endl;
        } 
        else if (desc->second.mode != W && desc->second.mode != RW) 
        {
            cerr << "write: error: " << args[1] << " not open for write." << endl;
        } 
        else if (desc->second.byte_pos + args[2].size() > max_size)
        {
            cerr << "write: error: File to large for inode." << endl;
        }
        else if (!basic_write(desc->second, args[2])) // ONLY reason!
        {
            cerr << "write: error: Insufficient disk space." << endl;
        }
    }
}

uint myFS::basic_write(Descriptor &desc, const string data) 
{
    const char *bytes = data.c_str();
    uint &pos = desc.byte_pos;
    uint bytes_to_write = data.size();              // expected to write
    uint bytes_written = 0;                         // already write
    auto inode = desc.inode.lock();
    uint &file_size = inode->size;                  
    uint &file_blocks_used = inode->blocks_used;
    uint new_size = max(file_size, pos + bytes_to_write);
    uint new_blocks_used = ceil(static_cast<double>(new_size)/block_size);
    uint blocks_needed = new_blocks_used - file_blocks_used;
    uint dbytes = direct_blocks * block_size;

    // expand the inode to indirect blocks if needed
    if (blocks_needed && blocks_needed + file_blocks_used > 2) 
    {
        // expand inode vec
        uint ivec_used = (ceil(min(file_blocks_used - 2, 0U) / static_cast<float>(direct_blocks)));
        uint ivec_new = (ceil((new_blocks_used - 2) / static_cast<float>(direct_blocks)));
        while (ivec_used < ivec_new)
        {
            inode->i_blocks->push_back(vector<uint>());
            ivec_used++;
        }
    }

    // find space
    vector<pair<uint, uint>> free_chunks;
    auto fl_it = begin(free_list);
    while (blocks_needed > 0) // can be more effecient
    {
        if (fl_it == end(free_list)) 
        {
            // 0 return because ran out of free space, find no space
            return 0;
        }
        if (fl_it->block_num > blocks_needed)  // chunk big enough to hold the rest of our write
        {
            free_chunks.push_back(make_pair(fl_it->pos, blocks_needed));
            fl_it->pos += blocks_needed * block_size;
            fl_it->block_num -= blocks_needed;
            break;
        }
        // a chunk, but will fill it and need more, then find another chunk
        free_chunks.push_back((make_pair(fl_it->pos, fl_it->block_num)));
        blocks_needed -= fl_it->block_num;
        auto used_entry = fl_it++;
        free_list.erase(used_entry);
    }

    // allocate blocks
    for (auto fc_it : free_chunks)
    {
        uint block_pos = fc_it.first;
        uint block_num = fc_it.second;
        for (uint k = 0; k < block_num; ++k, ++file_blocks_used, block_pos += block_size) 
        {
            if (file_blocks_used < direct_blocks) 
            {
                inode->d_blocks.push_back(block_pos);
            } 
            else 
            {
                uint i = ((file_blocks_used - direct_blocks) / direct_blocks);
                inode->i_blocks->at(i).push_back(block_pos);
            }
        }
    }

    // actually write our blocks
    while (bytes_to_write > 0) 
    {
        uint write_size = min(block_size - pos % block_size, bytes_to_write);
        uint write_dest = 0;
        if (pos < dbytes) 
        {
            write_dest = inode->d_blocks[pos / block_size] + pos % block_size;
        } 
        else 
        {
            uint i = (pos - dbytes) / (direct_blocks * block_size);
            uint j = (pos - dbytes) / block_size % direct_blocks;
            write_dest = inode->i_blocks->at(i)[j] + pos % block_size;
        }
        disk_file.seekp(write_dest);
        disk_file.write(bytes + bytes_written, write_size);
        bytes_written += write_size;
        bytes_to_write -= write_size;
        pos += write_size;
    }

    disk_file.flush();
    file_size = new_size;
    return bytes_written;
}

//D: change the pos of desc
//I: seek 
void myFS::seek(vector<string> args) 
{
    ops_exactly(2);
    uint fd;
    if ( !(istringstream(args[1]) >> fd)) 
    {
        cerr << "seek: error: Unknown descriptor." << endl;
        return;
    }
    auto desc_it = open_files.find(fd);
    if (desc_it == open_files.end()) 
    {
        cerr << "seek: error: File descriptor not open." << endl;
        return;
    }
    auto &desc = desc_it->second;
    uint pos;
    if (!(istringstream(args[2]) >> pos)) 
    {
        cerr << "seek: error: Invalid position." << endl;
    } 
    else if (pos > desc.inode.lock()->size)
    {
        cerr << "seek: error: Position outside file." << endl;
    } 
    else 
    {
        desc.byte_pos = pos;
    }
}


bool myFS::basic_close(uint fd) 
{
    auto kv = open_files.find(fd);
    if(kv == open_files.end())  // file do not open 
    {
        return false;
    }
    else 
    {
        kv->second.from.lock()->is_locked = false;
        open_files.erase(fd);
    }
    return true;
}

//TODO
//D: wrap basic_close
//I: 
void myFS::close(vector<string> args)
{
    ops_exactly(1);
    uint fd;

    if (! (istringstream (args[1]) >> fd)) 
    {
        cerr << "close: error: File descriptor not recognized" << endl;
    } 
    else
    {
        if (!basic_close(fd)) 
        {
            cerr << "close: error: File descriptor not open" << endl;
        }
        else 
        {
            cout << "closed " << fd << endl;
        }
    }
}

//D: mkdir, can not recrusive
void myFS::mkdir(vector<string> args)
{
    ops_at_least(1);
  /* add each new directory one at a time */
    for (uint i = 1; i < args.size(); i++)
    {
        auto path = parse_path(args[i]);    // final inode and constuct inode
        auto node = path->final_node;
        auto dirname = path->final_name;
        auto parent = path->parent_node;

        if (path->invalid_path)
        {
          cerr << "mkdir: error: Invalid path: " << args[i] << endl;
          return;
        }
        else if (node == root_dir)
        {
          cerr << "mkdir: error: Cannot recreate root." << endl;
          return;
        }
        else if (node != nullptr)
        {
          cerr << "mkdir: error: " << args[i] << " already exists." << endl;
          continue;
        }

        /* actually add the directory */
        parent->add_dir(dirname);
    }
}

//D: rm -r dir
//O: S/F
void myFS::rmdir(vector<string> args) 
{
    ops_at_least(1);

    for (uint i = 1; i < args.size(); i++) 
    {
        auto path = parse_path(args[i]);
        auto node = path->final_node;
        auto parent = path->parent_node;

        if (node == nullptr) 
        {
            cerr << "rmdir: error: Invalid path: " << args[i] << endl;
        } 
        else if (node == root_dir) 
        {
            cerr << "rmdir: error: Cannot remove root." << endl;
        } 
        else if (node == pwd) 
        {
            cerr << "rmdir: error: Cannot remove working directory." << endl;
        }
        else if (node->contents.size() > 0) 
        {
            cerr << "rmdir: error: Directory not empty." << endl;
        } 
        else if (node->type != dir)
        {
            cerr << "rmdir: error: " << node->name << " must be directory." << endl;
        } 
        else 
        {
            parent->contents.remove(node);
        }
    }
}

//D: print pwd
void myFS::printwd(vector<string> args) 
{
    ops_exactly(0);

    if (pwd == root_dir) 
    {
        cout << "/" << endl;
        return;
    }

    auto wd = pwd;
    deque<string> plist;
    while (wd != root_dir) 
    {
        plist.push_front(wd->name);
        wd = wd->parent.lock();
    }

    for (auto dirname : plist) 
    {
        cout << "/" << dirname;
    }
    cout << endl;
}


std::string myFS::getpwd(vector<string> args) 
{
    // ops_exactly(0);
    std::string str;
    if (pwd == root_dir) 
    {
        str="/";
        // cout << "/" << endl;
        return str;
    }

    auto wd = pwd;
    deque<string> plist;
    while (wd != root_dir) 
    {
        plist.push_front(wd->name);
        wd = wd->parent.lock();
    }
    // str+="/";
    for (auto dirname : plist) 
    {
        str+="/";
        str+=dirname;
    }
    return str;
    // cout << endl;

}

//D: change dir
//I: cd dir
void myFS::cd(vector<string> args) 
{
    ops_exactly(1);

    auto path = parse_path(args[1]);
    auto node = path->final_node;

    if (node == nullptr) 
    {
        cerr << "cd: error: Invalid path: " << args[1] << endl;
    } 
    else if (node->type != dir) 
    {
        cerr << "cd: error: " << args[1] << " must be a directory." << endl;
    }
    else
    {
        pwd = node;
    }
}

void myFS::link(vector<string> args) 
{
    ops_exactly(2);

    auto src_path = parse_path(args[1]);
    auto src = src_path->final_node;
    auto src_parent = src_path->parent_node;
    auto dest_path = parse_path(args[2]);
    auto dest = dest_path->final_node;
    auto dest_parent = dest_path->parent_node;
    auto dest_name = dest_path->final_name;

    if (src == nullptr) 
    {
        cerr << "link: error: Cannot find " << args[1] << endl;
    } 
    else if (dest != nullptr) 
    {
        cerr << "link: error: " << args[2] << " already exists." << endl;
    } 
    else if (src->type != file)
    {
        cerr << "link: error: " << args[1] << " must be a file." << endl;
    } 
    else if (src_parent == dest_parent) 
    {
        cerr << "link: error: src and dest must be in different directories." << endl;
    } 
    else 
    {
        auto new_file = DirEntry::make_de_file(dest_name, dest_parent, src->inode);
        dest_parent->contents.push_back(new_file);
    }
}

void myFS::unlink(vector<string> args) 
{
    ops_exactly(1);

    auto path = parse_path(args[1]);
    auto node = path->final_node;
    auto parent = path->parent_node;

    if (node == nullptr) 
    {
        cerr << "unlink: error: File not found." << endl;
    } 
    else if (node->type != file) 
    {
        cerr << "unlink: error: " << args[1] << " must be a file." << endl;
    } 
    else if (node->is_locked) 
    {
        cerr << "unlink: error: " << args[1] << " is open." << endl;
    } 
    else 
    {
        parent->contents.remove(node);
    }
}

void myFS::stat(vector<string> args) 
{
    ops_at_least(1);

    for (uint i = 1; i < args.size(); i++) 
    {
        auto path = parse_path(args[i]);
        auto node = path->final_node;

        if (node == nullptr) 
        {
            cerr << "stat: error: " << args[i] << " not found." << endl;
        }
        else 
        {
            cout << "  File: " << node->name << endl;
            if (node->type == file) 
            {
                cout << "  Type: file" << endl;
                cout << " Inode: " << node->inode.get() << endl;
                cout << " Links: " << node->inode.use_count() << endl;
                cout << "  Size: " << node->inode->size << endl;
                cout << "Blocks: " << node->inode->blocks_used << endl;
            } 
            else if(node->type == dir) 
            {
                cout << "  Type: directory" << endl;
            }
        }
    }
}

void myFS::ls(vector<string> args) 
{
    ops_exactly(0);
    for (auto dir : pwd->contents) 
    {
        cout << dir->name << endl;
    }
}

void myFS::cat(vector<string> args) 
{
    ops_at_least(1);

    for(uint i = 1; i < args.size(); i++) 
    {
        Descriptor desc;
        if(!basic_open(&desc, vector<string> {args[0], args[i], "r"}))
        {
        /* failed to open */
        continue;
        }

        auto size = desc.inode.lock()->size;
        read(vector<string>{args[0], std::to_string(desc.fd), std::to_string(size)});
        basic_close(desc.fd);
    }
}

void myFS::cp(vector<string> args) 
{
    ops_exactly(2);

    Descriptor src, dest;
    if(basic_open(&src, vector<string> {args[0], args[1], "r"})) 
    {
        if(!basic_open(&dest, vector<string> {args[0], args[2], "w"})) 
        {
            basic_close(src.fd);
        } 
        else 
        {
            auto data = basic_read(src, src.inode.lock()->size);
            if (!basic_write(dest, *data)) 
            {
                cerr << args[0] << ": error: out of free space or file too large"<< endl;
            }
            basic_close(src.fd);
            basic_close(dest.fd);
        }
    }
}

void tree_helper(shared_ptr<DirEntry> directory, string indent) 
{
    auto cont = directory->contents;
    if (directory->type == file) 
    {
        cout << directory->name << ": " << directory->inode->size << " bytes" << endl;
    }
    else 
    {
        cout << directory->name << endl;
    }
    if (cont.size() == 0) return;

    if (cont.size() >= 2) 
    {
        auto last = *(cont.rbegin());
        for (auto entry = cont.begin(); *entry != last; entry++) 
        {
            cout << indent << "├───";
            tree_helper(*entry, indent + "│   ");
        }
    }

    cout << indent << "└───";
    tree_helper(*(cont.rbegin()), indent + "    ");
}

void myFS::tree(vector<string> args)
{
  ops_exactly(0);

  tree_helper(pwd, "");
}

//D: file only import
void myFS::import(vector<string> args) 
{
//    ops_exactly(2);
    Descriptor desc;
    fstream in(args[1]);
    if(!in.is_open())
    {
        cerr << args[0] << ": error: Unable to open " << args[1] << endl;
        return;
    }
    if (basic_open(&desc, vector<string>{args[0], args[2], "w"})) 
    {
        string data((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
        if (!basic_write(desc, data)) 
        {
            cerr << args[0] << ": error: out of free space or file too large"<< endl;
        }
        basic_close(desc.fd);
    }
}

//D: file only export
void myFS::FS_export(vector<string> args) 
{
    ops_exactly(2);

    Descriptor desc;
    ofstream out(args[2], ofstream::binary);
    if (!out.is_open()) 
    {
        cerr << args[0] << ": error: Unable to open " << args[2] << endl;
        return;
    }

    if (basic_open(&desc, vector<string>{args[0], args[1], "r"})) 
    {
        unique_ptr<string> data = basic_read(desc, desc.inode.lock()->size);
        out << *data;
        basic_close(desc.fd);
    }
}
