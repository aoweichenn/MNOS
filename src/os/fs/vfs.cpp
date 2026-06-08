#include <stdexcept>
#include <utility>

#include <mnos/os/fs/vfs.hpp>

namespace
{
constexpr char VFS_PATH_SEPARATOR = '/';
constexpr const char* VFS_INVALID_PATH_MESSAGE = "vfs path must be absolute and normalized enough for SimpleFS";
constexpr const char* VFS_NOT_FOUND_MESSAGE = "vfs path was not found";
constexpr const char* VFS_EXPECTED_FILE_MESSAGE = "vfs operation requires a file";
constexpr const char* VFS_EXPECTED_DIRECTORY_MESSAGE = "vfs operation requires a directory";
constexpr const char* VFS_INVALID_MODE_MESSAGE = "vfs open mode is invalid for this operation";
constexpr const char* VFS_CLOSED_FILE_MESSAGE = "vfs file is not open";
constexpr const char* VFS_SEEK_PAST_END_MESSAGE = "vfs file seek cannot move past end";

[[nodiscard]] bool is_valid_path_segment(const std::string_view segment) noexcept
{
    return !segment.empty() && segment.size() <= mnos::os::fs::SIMPLE_FS_MAX_NAME_LENGTH && segment != "." &&
           segment != "..";
}
}

namespace mnos::os::fs
{
VfsNode::VfsNode(InodeNumber inode, const SimpleFsNodeKind kind, const std::uint64_t size_bytes) noexcept :
    inode_(inode),
    kind_(kind),
    size_bytes_(size_bytes)
{
}

InodeNumber VfsNode::inode() const noexcept
{
    return this->inode_;
}

SimpleFsNodeKind VfsNode::kind() const noexcept
{
    return this->kind_;
}

std::uint64_t VfsNode::size_bytes() const noexcept
{
    return this->size_bytes_;
}

bool VfsNode::is_file() const noexcept
{
    return this->kind_ == SimpleFsNodeKind::FILE;
}

bool VfsNode::is_directory() const noexcept
{
    return this->kind_ == SimpleFsNodeKind::DIRECTORY;
}

VfsFile::VfsFile(SimpleFileSystem& file_system, InodeNumber inode, const VfsOpenMode mode) noexcept :
    file_system_(&file_system),
    inode_(inode),
    mode_(mode)
{
}

InodeNumber VfsFile::inode() const noexcept
{
    return this->inode_;
}

VfsOpenMode VfsFile::mode() const noexcept
{
    return this->mode_;
}

std::uint64_t VfsFile::offset() const noexcept
{
    return this->offset_;
}

std::uint64_t VfsFile::size_bytes() const
{
    if (!this->is_open())
    {
        throw std::logic_error{VFS_CLOSED_FILE_MESSAGE};
    }
    return this->file_system_->metadata(this->inode_).size_bytes();
}

bool VfsFile::readable() const noexcept
{
    return this->mode_ == VfsOpenMode::READ_ONLY || this->mode_ == VfsOpenMode::READ_WRITE;
}

bool VfsFile::writable() const noexcept
{
    return this->mode_ == VfsOpenMode::WRITE_ONLY || this->mode_ == VfsOpenMode::READ_WRITE;
}

bool VfsFile::is_open() const noexcept
{
    return this->file_system_ != nullptr && this->inode_.is_valid() && this->mode_ != VfsOpenMode::COUNT;
}

void VfsFile::seek(const std::uint64_t offset)
{
    if (!this->is_open())
    {
        throw std::logic_error{VFS_CLOSED_FILE_MESSAGE};
    }
    if (offset > this->size_bytes())
    {
        throw std::invalid_argument{VFS_SEEK_PAST_END_MESSAGE};
    }
    this->offset_ = offset;
}

std::size_t VfsFile::read(const std::span<cpu::Byte> destination)
{
    if (!this->is_open())
    {
        throw std::logic_error{VFS_CLOSED_FILE_MESSAGE};
    }
    if (!this->readable())
    {
        throw std::invalid_argument{VFS_INVALID_MODE_MESSAGE};
    }

    const std::size_t byte_count = this->file_system_->read_file(this->inode_, this->offset_, destination);
    this->offset_ += static_cast<std::uint64_t>(byte_count);
    return byte_count;
}

std::size_t VfsFile::write(const std::span<const cpu::Byte> source)
{
    if (!this->is_open())
    {
        throw std::logic_error{VFS_CLOSED_FILE_MESSAGE};
    }
    if (!this->writable())
    {
        throw std::invalid_argument{VFS_INVALID_MODE_MESSAGE};
    }

    const std::size_t byte_count = this->file_system_->write_file(this->inode_, this->offset_, source);
    this->offset_ += static_cast<std::uint64_t>(byte_count);
    return byte_count;
}

Vfs::Vfs(SimpleFileSystem& file_system) noexcept : file_system_(file_system)
{
}

VfsNode Vfs::root() const
{
    return this->node_from_inode(this->file_system_.root_inode());
}

std::optional<VfsNode> Vfs::lookup(const std::string_view path) const
{
    const std::vector<std::string> components = this->split_absolute_path(path);
    InodeNumber current = this->file_system_.root_inode();
    if (components.empty())
    {
        return this->node_from_inode(current);
    }

    for (std::size_t component_index = std::size_t{0}; component_index < components.size(); ++component_index)
    {
        const std::optional<SimpleFsDirectoryEntry> entry =
            this->file_system_.lookup(current, components[component_index]);
        if (!entry.has_value())
        {
            return std::nullopt;
        }
        const bool is_last_component = component_index + std::size_t{1} == components.size();
        if (!is_last_component && !entry->is_directory())
        {
            return std::nullopt;
        }
        current = entry->inode();
    }
    return this->node_from_inode(current);
}

VfsNode Vfs::create_file(const std::string_view path)
{
    const ParentPath parent_path = this->resolve_parent(path);
    const InodeNumber inode = this->file_system_.create_file(parent_path.parent, parent_path.leaf);
    return this->node_from_inode(inode);
}

VfsNode Vfs::create_directory(const std::string_view path)
{
    const ParentPath parent_path = this->resolve_parent(path);
    const InodeNumber inode = this->file_system_.create_directory(parent_path.parent, parent_path.leaf);
    return this->node_from_inode(inode);
}

VfsFile Vfs::open_file(const std::string_view path, const VfsOpenMode mode)
{
    if (mode == VfsOpenMode::COUNT)
    {
        throw std::invalid_argument{VFS_INVALID_MODE_MESSAGE};
    }
    const std::optional<VfsNode> node = this->lookup(path);
    if (!node.has_value())
    {
        throw std::out_of_range{VFS_NOT_FOUND_MESSAGE};
    }
    if (!node->is_file())
    {
        throw std::invalid_argument{VFS_EXPECTED_FILE_MESSAGE};
    }
    return VfsFile{this->file_system_, node->inode(), mode};
}

std::vector<SimpleFsDirectoryEntry> Vfs::read_directory(const std::string_view path) const
{
    const std::optional<VfsNode> node = this->lookup(path);
    if (!node.has_value())
    {
        throw std::out_of_range{VFS_NOT_FOUND_MESSAGE};
    }
    if (!node->is_directory())
    {
        throw std::invalid_argument{VFS_EXPECTED_DIRECTORY_MESSAGE};
    }
    return this->file_system_.read_directory(node->inode());
}

std::vector<std::string> Vfs::split_absolute_path(const std::string_view path) const
{
    if (path.empty() || path.front() != VFS_PATH_SEPARATOR)
    {
        throw std::invalid_argument{VFS_INVALID_PATH_MESSAGE};
    }

    std::vector<std::string> components;
    std::size_t segment_start = std::size_t{1};
    while (segment_start <= path.size())
    {
        const std::size_t separator = path.find(VFS_PATH_SEPARATOR, segment_start);
        const std::size_t segment_end = separator == std::string_view::npos ? path.size() : separator;
        if (segment_end > segment_start)
        {
            const std::string_view segment = path.substr(segment_start, segment_end - segment_start);
            if (!is_valid_path_segment(segment))
            {
                throw std::invalid_argument{VFS_INVALID_PATH_MESSAGE};
            }
            components.emplace_back(segment);
        }
        if (separator == std::string_view::npos)
        {
            break;
        }
        segment_start = separator + std::size_t{1};
    }
    return components;
}

Vfs::ParentPath Vfs::resolve_parent(const std::string_view path) const
{
    std::vector<std::string> components = this->split_absolute_path(path);
    if (components.empty())
    {
        throw std::invalid_argument{VFS_INVALID_PATH_MESSAGE};
    }

    ParentPath result;
    result.parent = this->file_system_.root_inode();
    result.leaf = std::move(components.back());
    components.pop_back();

    for (const std::string& component : components)
    {
        const std::optional<SimpleFsDirectoryEntry> entry = this->file_system_.lookup(result.parent, component);
        if (!entry.has_value())
        {
            throw std::out_of_range{VFS_NOT_FOUND_MESSAGE};
        }
        if (!entry->is_directory())
        {
            throw std::invalid_argument{VFS_EXPECTED_DIRECTORY_MESSAGE};
        }
        result.parent = entry->inode();
    }
    return result;
}

VfsNode Vfs::node_from_inode(const InodeNumber inode) const
{
    const SimpleFsInodeMetadata metadata = this->file_system_.metadata(inode);
    return VfsNode{metadata.number(), metadata.kind(), metadata.size_bytes()};
}
}
