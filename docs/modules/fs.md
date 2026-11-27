# fs

The descriptor-relative file system: directories opened as roots, and every other operation
relative to a descriptor, resolved beneath it. Namespace `core::fs`, directory `src/core/fs/`,
target `core::fs`. It links [base](base.md), [async](async.md) and [platform](platform.md), and
`ntdll` and `advapi32` on Windows. Built with `CORE_CPP_WITH_FS` (ON), natively only: Emscripten and
WASI have no descriptors to hold.

What it is for: a host that hands a guest, a plugin or a request handler one directory and must be
sure that nothing reached through it lies outside it. Every operation is an `async::Task`, every
blocking system call runs on an executor the embedder injects, and every refusal is an `FsError`
value.

| Header | What it has |
|---|---|
| `<core/fs/Descriptor.hpp>` | `IDescriptorFileSystem` (`openPreopen()`, `relation()`), `IDescriptor` (the byte, metadata and `*At` operations), `IDirectoryStream`; `DescriptorFlags`, `OpenFlags`, `CreationAccess`, `SymlinkPolicy`, `Advice`, `StreamEnd`, `DescriptorType`, `DescriptorStat`, `FileTime`, `NewTimestamp` (`NoChange`, `SetToNow` or a `FileTime`), `DirectoryEntry`, `MetadataHash`, `ReadChunk`, `Relation`; `hasFlag()` and `operator|` for both masks |
| `<core/fs/NativeFileSystem.hpp>` | `makeNativeDescriptorFileSystem(home, blocking, wallClock, random, options)`, `NativeFileSystemOptions`, `BeneathStrategy`, `DefaultMaxSymlinkHops` (40), `DefaultMaxAncestorDepth` (4,096) |
| `<core/fs/FsError.hpp>` | `FsErrorCode` and its `toString()`, `FsError` (a category, the raw errno, Win32 error or NTSTATUS, and a context), `FsResult<T>`; the classification tables `systemErrorTable()`, `ntStatusTable()`, `noFollowSymlinkErrnos()`, and `fsErrorFromSystemCode()`, `fsErrorFromNtStatus()` |
| `<core/fs/PathPolicy.hpp>` | `PathPolicy` (`PathEncoding`, `CaseRule`, `TrailingDotsAndSpaces`), `posixPathPolicy()`, `windowsPathPolicy()`, `nativePathPolicy()`, and `isRootedLinkTarget()` |
| `<core/fs/Rights.hpp>` | `Operation`, `RightsRow`, `rightsTable()`, `checkRights()`, `checkOpenRights()`, `grantedFlags()` |
| `<core/fs/AccessScope.hpp>` | `AccessScope`, `AccessFacts`, `AccessEntry` and `classifyAccess()`, the pure decision of who may reach an object |
| `<core/fs/testing/InMemoryDescriptorFileSystem.hpp>` | `testing::InMemoryDescriptorFileSystem` and `testing::IdentityModel`: the same contract over nodes in memory |

## Off the loop

epoll and kqueue cannot wait on a regular file, so "non-blocking" here means **off the loop's
thread**. `makeNativeDescriptorFileSystem()` takes two executors: every system call of the file
system's operations and of its descriptors runs on `blocking` through [`async::offload`](async.md#offloading-a-blocking-call),
and the caller resumes on `home`. Building the file system is the exception: it runs on the calling
thread and makes the few calls that set it up (on Linux the `openat2` probe and the open of
`/proc/self`), so build it before the loop runs or off it. Nothing in the module creates a thread;
`blocking` is typically an `async::ThreadPoolExecutor` the embedder owns.

- **A stop is honoured before the hop, never after.** A flow stopped before it hops throws
  `async::OperationCancelled` and nothing runs; once the call has run, its result is returned, so a
  completed write's count or an opened handle is never dropped.
- **A handle is shared by every call in flight on it.** A descriptor dropped while a call of it is
  parked on `blocking` keeps its native handle open until that call has returned, so the call never
  reaches another file that reused the number. The close itself is queued on `blocking` too.
- **Teardown: drain or stop `blocking` before `home` is destroyed.** A close still queued when
  `blocking` is destroyed runs there and then.
- `home` and `blocking` may be the same executor. Every system call then runs on the loop's
  thread: a single-threaded embedder's choice, visible at construction.
- **A read is bounded by the host.** `IDescriptor::read` clamps what it is asked for to
  `NativeFileSystemOptions::maxReadChunk` (1 MiB by default; 0 is refused at construction), so a
  caller cannot make the host allocate what it asks for. Buffers are owned across the hop: `read`
  returns a `std::vector<std::byte>`, `write` and `append` take one by value.
- **A directory is read in batches.** `IDescriptor::readDirectory` opens an `IDirectoryStream` that
  holds one batch of entries at a time, never the whole listing.

`NativeDescriptor_test.cpp`'s *native: every descriptor operation parks on the blocking executor*
runs every `IDescriptor` operation on a manual executor and holds that each one parks there.

## Beneath resolution

**The kernel never resolves a path's final component**, and never resolves anything from outside
the descriptor. Every final call is made without following (`AT_SYMLINK_NOFOLLOW`, `O_NOFOLLOW`,
or a held handle: `fstat`, `futimens`, `FileBasicInfo` by handle); where a call says
`SymlinkPolicy::Follow`, following is the walk's splice, for the last component too.

| Input | Answer |
|---|---|
| `""` | `NoEntry` |
| `"."`, `"a/."` | the directory itself, resolved to a held handle and operated on through it |
| an absolute path; `..` above the descriptor; a symbolic link whose target is rooted (`isRootedLinkTarget()`) or leaves the descriptor; `symlinkAt` with a rooted target; a Windows reserved device name | `NotPermitted` |
| a NUL, a character the policy forbids, a trailing dot or space where the policy refuses it, a name holding what the kernel would split on (`/`; `\` and `:` on Windows) | `Invalid` |
| a name that is not UTF-8; a directory entry that cannot be represented as UTF-8 | `IllegalByteSequence` |
| `linkAt` with `SymlinkPolicy::Follow` | `Invalid`, as in wasmtime |
| more than `maxSymlinkHops` (40) links in one resolution; opening a final link with `NoFollow` | `Loop` |

`BeneathStrategy::Best` is `openat2(RESOLVE_BENEATH | RESOLVE_NO_MAGICLINKS)` on Linux, probed once
per file system; `ComponentWalk`, one component at a time over held directory handles (`openat` with
`O_NOFOLLOW` on POSIX, `NtCreateFile` with a `RootDirectory` on Windows), is used everywhere else
and wherever the probe finds no `openat2`. Under `openat2`, `EXDEV` is `NotPermitted`, `EAGAIN` is
retried 8 times and then that call walks, and an `ENOSYS`, `EPERM` or `EINVAL` after the probe makes
that call walk too. The choice is the file-system instance's, never a global. A Linux build needs
no `openat2` in the system headers: `struct open_how`, the two `RESOLVE_` bits and system call 437
are spelled in `linux/Openat2.hpp`, the ABI the kernel keeps, and the test checks them against
`<linux/openat2.h>` where it is present.

**Name rules are data.** A `PathPolicy` is one row: the separators of a path and of a link target,
the reserved names, the volume separators, the forbidden characters, the encoding, the case rule and
the trailing dot or space rule. `nativePathPolicy()` is the build's row, overridable through
`NativeFileSystemOptions::policy`; the contract suite runs both rows on every OS.

| | `posixPathPolicy()` | `windowsPathPolicy()` |
|---|---|---|
| Separators of a path | `/` | `/` and `\` |
| Separators of a link target | `/` | `/` and `\` |
| Volume separators | none | `:` (a first component holding one names a volume) |
| Forbidden | NUL only (refused in every text, whatever the row) | NUL, `<>:"?*`, the vertical bar and the control characters 1 to 31 |
| Reserved stems | none | `CON PRN AUX NUL COM0-9 LPT0-9 COM¹²³ LPT¹²³ CONIN$ CONOUT$`, in any case; the stem is what precedes the first dot, trailing spaces dropped (`nul.txt`, `COM1 .log`) |
| Encoding | the UTF-8 bytes | UTF-16 |
| Case | sensitive | insensitive (ASCII folded) |
| Trailing `.` or space | allowed | `Invalid` |

`openPreopen()` takes a `std::filesystem::path` the host chose, which is not held to a policy.

On Windows a junction, a mount point and every other reparse point that names an object by an
absolute path is a link that is never followed (`NotPermitted`), and `readlinkAt` of one is
`NotPermitted` too, so no host path is handed out; the full list of Windows differences is the doc
comment of `makeNativeDescriptorFileSystem()`.

### Rooted link targets

`isRootedLinkTarget(target, policy)` answers whether a symbolic link's target names a root rather
than a place relative to the link: it starts with one of the policy's target separators, or its
first component holds one of its volume separators. Every link a walk follows, every `symlinkAt` and
every `readlinkAt` asks it, so the rule has one definition: a rooted target the host planted is read
by nobody (`NotPermitted`, as WASI 0.3.2's `readlink-at` answers), in the in-memory model as natively.

| Target | POSIX rules | Windows rules |
|---|---|---|
| `/etc`, `/x` | rooted | rooted |
| `\x` | relative | rooted |
| `C:`, `C:x`, `C:\x` | relative | rooted |
| `\\server\share`, `\\?\C:\` | relative | rooted |
| `a/b`, `a\b`, `../x`, `..\x` | relative | relative |
| `""` | not rooted | not rooted |

A relative target that climbs out (`../x`) is not rooted; resolution refuses it where it leaves.

## Rights

A descriptor carries `DescriptorFlags`, and **every implementation enforces them**, the in-memory
one included, from one table: `rightsTable()` names, per `Operation`, the flags the descriptor it is
called on must carry one of. A refusal is `NotPermitted`, before anything is touched.

| Needs | Operations |
|---|---|
| nothing | `advise`, `sync`, `syncData`, `stat`, `type`, `metadataHash` |
| `Read` | `read`, `readDirectory`, `openAt`, `statAt`, `readlinkAt`, `metadataHashAt` |
| `Write` | `write`, `append`, `setSize` |
| `Write` or `MutateDirectory` | `setTimes` (a file has only the first to offer, a directory only the second) |
| `MutateDirectory` | `createDirectoryAt`, `setTimesAt`, `linkAt` and `renameAt` (on both directories), `removeDirectoryAt`, `symlinkAt`, `unlinkFileAt` |

`checkOpenRights(parent, open, child)` bounds what a child opened by `openAt` may carry: `Read`
needs the parent's `Read`; `Write` or `MutateDirectory`, and `Create`, `Truncate`, `Exclusive` or
`OwnerOnly`, need the parent's `MutateDirectory`; the three sync flags are a mode, not a right. Then,
as wasmtime orders it: `Directory` with `Create`, `Truncate`, `Exclusive` or `OwnerOnly` is
`Invalid`, a directory never carries `Write` (`IsDirectory`), and `OwnerOnly` without both `Create`
and `Exclusive` is `Invalid`. `grantedFlags()` drops `MutateDirectory` from a descriptor that turns
out to be a file.

**A descriptor of another implementation, or of another instance, is foreign.** `linkAt` and
`renameAt` with one answer `CrossDevice`, `isSameObject` answers `false`, and `relation()` answers
`CrossDevice`. No raw handle crosses the interface; a decorator passes its inner descriptor.

## Owner-only creation and access scope

For a cache or a key that no other user may read, at any moment:

- `openAt(…, OpenFlags::Create | OpenFlags::Exclusive | OpenFlags::OwnerOnly, …)` creates a file
  no other user can reach under its final name or before it; `OwnerOnly` without `Create` and
  `Exclusive` is `Invalid`.
- `createDirectoryAt(path, CreationAccess::OwnerOnly)` does the same for a directory;
  `CreationAccess::Default` is what the system gives (POSIX `0777` less the umask, Windows the
  inherited DACL).

| OS | How |
|---|---|
| Linux | mode `0600`/`0700` given to `openat`/`mkdirat` itself; a default ACL cannot widen it, since each entry it hands down is capped by the mode |
| macOS, FreeBSD | an inherited ACL could widen it, so the object is made inside a fresh staging directory beside it (created `0700`, its inherited ACL stripped and its access verified before anything is made in it), stripped and verified itself, then linked (a file) or renamed over an empty placeholder (a directory) to its final name, which fails where something exists |
| Windows | owned by the process token's user, with a protected DACL (nothing inherited) granting that user alone, given to `NtCreateFile` itself; a directory's entry is inheritable. The access is read back through the new object's own handle: a file system that keeps no security descriptor (FAT, exFAT) drops it silently, and the object is then deleted through that handle |

Nothing has been written to the object when the call returns. Where a step fails, what was made is
removed through the parent's handle and the call fails: `NotPermitted` where the access read back
is not `OwnerOnly`, else what refused the step.

**`DescriptorStat::access` says who may reach an object**, read from the handle the stat was made
through. Each OS has a reader that fills one OS-neutral `AccessFacts`, and `classifyAccess()` is the
one decision, pure and tested with every OS's rows on every OS:

| OS | Read from the handle | `OwnerOnly` | `ForeignOwner` | `Shared` | `nullopt` |
|---|---|---|---|---|---|
| Linux | `fstat`, and `fstatfs` and the NFSv4 ACL attributes | owned by the effective uid, `mode & 077 == 0` (POSIX ACL entries are bounded by the group bits) | another uid owns it | any group or other bit | an NFS or SMB mount, an object with an NFSv4 ACL (ZFS `acltype=nfsv4`); every `statAt` |
| OpenBSD, DragonFly | `fstat` | as Linux | another uid owns it | any group or other bit | never |
| NetBSD, other POSIX | nothing (their ACLs are not read) | never | never | never | always, so owner-only creation is refused |
| FreeBSD | `fstat`, the NFSv4 ACL | as Linux, and the ACL absent or trivial | another uid owns it | a mode bit, or a non-trivial ACL | the ACL cannot be read; every `statAt` |
| macOS | `fstat`, the extended ACL | as Linux, and no extended ACL entry | another uid owns it | a mode bit, or any extended ACL entry | the ACL read fails; every `statAt` |
| Windows | owner and DACL, on a handle opened with `READ_CONTROL`; the token's user | owned by the user, and every ALLOW entry, explicit or inherited, names the user, LocalSystem or BUILTIN\Administrators | another owner, BUILTIN\Administrators included | a NULL DACL; an ALLOW entry granting any right to another SID; an entry of unknown type | the DACL withholds `READ_CONTROL` |

DENY entries never widen access and are ignored. **`Shared` is zero, and `nullopt` means "cannot
judge"**: a consumer guarding a secret refuses `nullopt` as it refuses `Shared`. Nothing here is
decided by name; the facts are read off the handle the caller then uses.

`core::constantTimeEquals()` and `core::secureZero()`, which such a consumer also needs, are in
[base](base.md#constant-time).

## Containment: `relation()`

`IDescriptorFileSystem::relation(first, second)` answers whether one opened directory is, contains
or lies beneath another: `Same`, `FirstContainsSecond`, `SecondContainsFirst` or `Unrelated`. It is
what a host needs to refuse a grant that would expose a directory it keeps for itself (a key
directory under a granted home directory), and it decides **on the opened handles, never on path
text**: a symbolic link or a junction on either path, or a rename after the open, changes nothing;
what counts is where each directory is now.

`Same` is zero, so a zero-initialized `Relation` refuses in a guard that admits only `Unrelated`.
Both descriptors are read before the task first suspends; neither needs to outlive it.

### How it decides

Each directory's real parents are walked up to the root of the process's view and compared by
identity:

- **POSIX**: `(st_dev, st_ino)`. Each parent is `openat(current, "..", O_DIRECTORY | O_NOFOLLOW)`
  (`O_PATH` where the system has it, so a directory the user may search but not read is walked
  through), until `..` is the directory itself. The kernel's own `..` crosses mounts upward, bind
  mounts included.
- **Windows**: volume serial number and 128-bit file id (`FILE_ID_INFO`). `NtCreateFile` has no
  `..` relative to a handle, so the walk is by name: the handle's final path by volume GUID, every
  junction and link resolved, then each ancestor prefix opened for its attributes only, without
  following a reparse point. At a volume's root the walk goes on from each folder the volume is
  mounted on; a drive letter is a root. Identities count only on NTFS and ReFS
  (`StableFileIdRows.cpp`); FAT, exFAT, every share and every other file system, and a zero id, are
  `Unsupported`.
- **The in-memory model**: its nodes' parent links; `IdentityModel::Unavailable` answers
  `Unsupported`, so a consumer tests its fail-closed path on every OS.

Each walk stops after `NativeFileSystemOptions::maxAncestorDepth` (4,096) parents with `Loop`, and
grows its chain per step, so a deep tree costs what it walks. **A containment the walks meet is
always real.** An `Unrelated`, though, is only as good as the walks' view of the tree, and the
mounts a walk leaves are where that view has holes.

### Mounts that hide directories

A mount that shows only a subtree of its file system hides that file system's directories above
the subtree from a walk that leaves it: the kernel's `..` at the mount's root goes to the directory
it is mounted on. A bind mount of `/data/a` at `/m` is the plain case: a walk from `/m/x` reaches
`/`, never `/data`, so `/data` and `/m/x` would look unrelated while one contains the other. Every
mount a walk leaves is therefore kept, and an `Unrelated` stands only where none of them could hide
the other directory; otherwise the answer is `Unsupported`.

**Linux** (5.8 or later, for `STATX_MNT_ID`; an older kernel names no mount, and every relation that
leaves one is `Unsupported`) names the mount of each directory by `statx(STATX_MNT_ID)` and looks it up in
`/proc/self/mountinfo`, read through a handle on `/proc/self` opened when the file system was built:

- the file system is the mount's superblock device (mountinfo's `major:minor`), never `st_dev`,
  which a btrfs subvolume changes;
- what the mount shows is mountinfo's *root* column: `/` for the whole file system, a path for a
  subtree (a bind mount of a subdirectory, a btrfs subvolume);
- Linux's `..` leaves a mount only at its root.

So an `Unrelated` across a mount left at its root stands where the mount shows its whole file system;
where the other directory is on that same mount; where it is on another file system; or where it is
reached through another mount of the same file system whose root is **not above** the left mount's
root. Two btrfs subvolumes mounted at `/` (root `/root`) and `/home` (root `/home`) decide against
each other; the bind mount above (root `/data/a`, the other directory under a mount of root `/`)
is `Unsupported`. A mount missing from mountinfo, or a line that does not parse, is known by its id
alone, a mountinfo over 16 MiB is not read, and `/proc/self` that could not be opened makes every
relation `Unsupported`.

**macOS and the BSDs** name a mount by `fstatfs`: its file system id and where it is mounted
(`f_mntonname`); the file system is `st_dev`. These systems say nowhere which subtree a mount
shows, so another mount of the same file system never decides: an `Unrelated` stands where the
other directory is on the mount left, or on another file system.

**A file system that may show another's directories as its own** has no identity that says what it
shows, so a walk that leaves one, or a directory on one, decides only on that same mount and is
otherwise `Unsupported`. The types are a table per system:

| System | Types that may re-expose another file system |
|---|---|
| Linux | `overlay`, `aufs`, `shiftfs`, every `fuse*` type (sshfs, bindfs, a passthrough), every `nfs*` type, `cifs`, `smb3` |
| macOS, BSDs | `nullfs`, `unionfs`, every `fuse*` type, `macfuse`, `osxfuse`, `bindfs`, every `nfs*` type (fuse-t reports `nfs`), `smbfs`, `afpfs`, `webdav` |

A network share is in the table because it may be a loopback export of this machine. A type that
cannot be read counts as one. sshfs fails closed with the rest.

**macOS firmlinks.** `/Users`, `/private` and the other firmlinks lead from the system volume into
the Data volume below its root, and their `..` is the system volume's root. A walk from
`/Users/me/x` therefore leaves the Data volume below its root, and skips (a) the Data volume's
directories above the one it left from and (b) every directory on the Data volume's mount point
path: `/System`, `/System/Volumes` and `/System/Volumes/Data` (the volume's root as that path
reaches it). A directory is its mount's root only where its identity is that of what `f_mntonname`
names (`ATTR_DIR_MOUNTSTATUS` is asked first); otherwise the step left below the root, and the
skipped set (b) is read by `(st_dev, st_ino)`, each component of the mount point path opened
without following from the root handle. Where the directory left from is a child of the volume's
root, (b) is all that was skipped; otherwise the volume's own directories between are unknown.
Unless the other directory's walk passed through the directory left from (it then lies at or below
it, and is none of what was skipped), the answer is `Unsupported` where nothing could be read,
where the other directory is one of the skipped set, or, where more than the set may have been
skipped, where it is on the Data volume. A home directory against `/private/tmp` stays decided; a
grant of `/System/Volumes` against a directory in the home directory is `Unsupported`. This rule is
checked before every shortcut, since a skipped directory on the mount point path is on another
volume.

Any other POSIX system knows a mount by its device alone: every step that changes device is taken
to leave below the root, from a type that may re-expose, and every `Unrelated` past a change of
device is `Unsupported`. Windows walks by final path and through every folder a volume is mounted
on, so it needs none of this.

### Mounts beneath a directory

A walk goes up, so it never sees a mount **beneath** either directory, and such a mount can put the
other directory inside it: a bind mount of `/secret` at `/home/me/work/m` puts `/secret/key` beneath
a grant of `/home/me/work`, though neither walk meets the other. So before an `Unrelated` stands,
the mount table is read and every mount point strictly beneath either directory is opened; the
answer is `Unsupported` where one of them is of the other directory's file system, of a type that
may re-expose another (the table above), where the other directory's own file system may, or where
either file system cannot be named.

- **The table**: Linux's `/proc/self/mountinfo` (its mount point column), refused whole if one line
  does not parse; macOS and the BSDs' `getfsstat` (never `getmntinfo`, whose buffer every thread
  shares). Any other POSIX system lists no mounts, so no `Unrelated` stands there at all.
- **Beneath, by identity**: each mount point's path is followed from the root one component at a
  time, never through a link. A component is told apart by `(st_dev, st_ino)` from `fstatat` before
  it is entered, which needs search permission on its parent alone: the path stays on the
  directory's own walked ancestry until it reaches the directory itself, and the first component
  that is neither ends the check without entering it -- so a mount under another user's private
  directory costs nothing. A component told apart is entered only as that directory: `fstat` on the
  handle `openat` returned must give the identity `fstatat` did, and a rename between the two is
  `Busy`. A component beneath the directory that cannot be opened is refused.
- **The mount that opens**: the path must open onto the mount the table named (Linux: its mount id;
  macOS and the BSDs: its file system id and `f_mntonname`). One made over it, a row that moved, or
  a mount stacked under another at one place, is `Unsupported`.
- **The table is read again** once both scans are done, and a difference is `Busy`.

A mount at the directory itself is not beneath it: an opened directory shows what it showed when it
was opened, and a mount made over its path later is not reached through the handle.

### Races

**Each answer is checked after the walk.** On POSIX both directories are walked twice, and any
difference between the passes is `Busy`; on Windows every handle's and every ancestor's final path
is read again once the walks are done, and a change, or a failure on the way a change could
explain, is `Busy`. What remains, documented rather than detected:

- **ABA on POSIX.** A directory moved away and back between the two passes is not seen; each chain
  is still one that existed.
- **A mount path changed after the second pass.** The other directory's mount is read once both
  passes are done; a remount, or a rename of a component of a mount point path, between the second
  pass and that read is not seen. On macOS that path is on the sealed system volume, so this needs
  root with System Integrity Protection off.
- **A mount beneath a directory whose path no longer reaches it.** The scan follows each mount
  point's path from the root, so a mount the path cannot reach -- made beneath a directory before
  another mount was made over that directory's path, or beneath a second path a bind mount shows the
  same directory at -- is not seen from that path. Making either needs the privilege to mount.
- **A mount made after the answer.** The scan sees the mount table as it was while it ran; a mount
  made beneath either directory afterwards is not seen, and no answer can foresee one. A host asks
  at the moment it grants and holds the grant to that answer: Isoline's preopen grant (P14a)
  evaluates the relation at grant time.
- **The directory the Data volume is mounted over** (the system volume's own `/System/Volumes/Data`)
  is not in the macOS skipped set, which reads what the mount point path reaches: the Data volume's
  root. It cannot be opened once the volume is mounted over it at boot, so no descriptor names it.

### What the answer means for a caller

| Answer | Meaning | What a guard does |
|---|---|---|
| `Same`, `FirstContainsSecond`, `SecondContainsFirst` | the containment is real | refuses the grant it guards against |
| `Unrelated` | neither lies beneath the other, no mount left on the way can hide one from the other, and no mount beneath either can show the other | admits |
| `Unsupported` | this layout cannot be decided: no stable identities (FAT, a share, `IdentityModel::Unavailable`), a mount of a subtree that may hide one from the other, a mount beneath one that may show the other's file system, a re-exposing file system, or a fact the system would not give | refuses. It is a property of the layout, not a transient: asking again gives the same answer, so say which grant could not be checked |
| `Busy` | a rename during the walk could have changed the answer | may ask again; refuses if it cannot |
| `Loop` | more than `maxAncestorDepth` parents | refuses |
| `NotDirectory` | either is not a directory | refuses |
| `CrossDevice` | a descriptor of another file system instance | refuses |
| `Access` and other system errors | a parent the user may not search, or what refused opening one | refuses |

Only `Unrelated` admits. A guard written as `if (relation == Relation::Unrelated) admit();` over a
`FsResult<Relation>` that holds a value fails closed on every other answer, and on a
zero-initialized one.

## The in-memory model

`testing::InMemoryDescriptorFileSystem(home, blocking, clock, policy, maxReadChunk, identities,
maxAncestorDepth)` is held to the same contract as the native one, through the same code where
there is code to share: paths are planned and links spliced by the walker's own functions under the
policy it is given, rights come from `rightsTable()` and `checkOpenRights()`, and the policy's case
rule decides whether two names are one. Every operation suspends through `async::offload`, as the
native one does, so a cancellation or teardown case over it sees the parked state it is about.
`addDirectory()`, `addFile()` and `addSymlink()` build a tree; `setAccessFacts()` models any OS's
owner and ACL. A write or a size beyond 1 GiB is `FileTooLarge`.

## Limits

- **No change notification, no advisory lock, no memory map**: `IDescriptor` has none.
- `statAt` on macOS and FreeBSD reports `access` as `nullopt`: an ACL is read from a handle or not
  at all.
- Windows needs `SeCreateSymbolicLinkPrivilege` for `symlinkAt`, refuses `renameAt` of a directory
  over an existing empty one where POSIX replaces it, and refuses a reparse point a filter serves by
  redirecting the open elsewhere (a container's placeholders).

## Tests

| Test | Binary | Labels | What |
|---|---|---|---|
| `core-cpp.fs` | `core-cpp-fs-test` | `core-cpp`, `fs` | the contract suite over both the native and the in-memory file system, under both path policies; the escape table and the mid-walk swap (`DescriptorEscape_test.cpp`); every operation parking on the blocking executor; the access-scope rows; `relation()` natively per OS, Linux mounts in a user namespace of their own and the macOS firmlinks included |
| `core-cpp.fs-alloc`, `core-cpp.fs-memory`, `core-cpp.fs-relation-memory` | `core-cpp-fs-alloc-test`, `core-cpp-fs-memory-test`, `core-cpp-fs-relation-memory-test` | `core-cpp`, `fs` | with the global allocation functions replaced: a queued close that cannot be allocated, and what a directory stream and a relation walk hold. POSIX only, and not under a sanitizer |

The Linux mount rows (`linux/Relation_test.cpp`) run in a user and mount namespace of their own
and SKIP where the system refuses one; CI sets `CORE_TEST_REQUIRE_USERNS=1`, which turns that skip
into a failure.
