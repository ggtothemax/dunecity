<?php
declare(strict_types=1);

/** Immutable Workshop snapshots. All names on disk are server-generated or validated hashes.
 * One stable lock serializes quota accounting, ownership checks and revision allocation. File
 * bodies are staged privately; a revision becomes discoverable only after every SHA-256 matches.
 * This is content distribution, never a gameplay-packet relay.
 */
final class Content
{
    public const MAX_MANIFEST = 255 * 1024;
    public const CHUNK = 65536;
    private const MAX_FILES = 4096;
    private const MAX_FILE = 128 * 1024 * 1024;
    private const MAX_TOTAL = 2147483648;
    private const UPLOAD_TTL = 86400;
    private const MAX_MAP_INI = 1048576;
    private const MAX_MAP_LINES = 65536;
    /** The largest map side the client itself accepts; anything beyond it is not a dimension. */
    private const MAX_MAP_SIDE = 2048;
    /** A mod folder name can never contain '?' or '*', so both are safe filter/row markers:
     * '?' is retained for old clients, '*' aliases the vanilla category.
     */
    private const MOD_UNKNOWN = '?';
    private const MOD_BASE_GAME = '*';
    /** Map.ini sections that each contribute one playable slot, exactly as the client counts them. */
    private const PLAYER_SECTIONS = ['atreides', 'ordos', 'harkonnen', 'fremen', 'mercenary', 'sardaukar',
        'rebels', 'custom', 'wildspade', 'kleshmersh', 'tharpique',
        'player1', 'player2', 'player3', 'player4', 'player5', 'player6',
        'player7', 'player8', 'player9', 'player10', 'player11', 'player12'];
    private const CITY_BUILDINGS = ['residential zone', 'zone residential', 'commercial zone', 'zone commercial', 'industrial zone', 'zone industrial', 'road', 'power line', 'powerline', 'nuclear', 'nuclear plant', 'police station', 'police', 'stadium', 'airport'];
    private const TORNIE_BUILDINGS = ['advanced windtrap 3x3', 'advanced windtrap', 'advanced wind trap', 'advanced wind trap 3x3', 'advanced windtrap 2x3', 'advanced windtrap mk2', 'advanced wind trap mk2', 'advanced wind trap 2x3', 'advanced windtrap 3x2', 'advanced windtrap mk3', 'advanced wind trap mk3', 'advanced wind trap 3x2', 'worfinery', 'tech center', 'techcenter', 'scoutpost', 'scout post', 'green post', 'sentinel post', 'avant-poste', 'avant poste', 'flamepost', 'flame post', 'chemipost', 'chemi post', 'love factory', 'lovefactory', 'chaos factory', 'chaosfactory'];
    private string $dir;

    public function __construct(private readonly Config $config)
    {
        // The ingress Rate/Store call has already bootstrapped and verified the private parent.
        $this->dir = $config->stateDir() . '/content';
        foreach ([$this->dir, $this->dir . '/blobs', $this->dir . '/manifests', $this->dir . '/uploads',
                  $this->dir . '/maps'] as $dir) {
            if (!file_exists($dir) && !is_link($dir)) {
                if (!@mkdir($dir, 0700) && !is_dir($dir)) self::unavailable();
            }
            $stat = @lstat($dir);
            if (is_link($dir) || !is_dir($dir) || $stat === false || ($stat['mode'] & 077) !== 0
                || (function_exists('posix_geteuid') && $stat['uid'] !== posix_geteuid())) self::unavailable();
        }
    }

    private static function unavailable(): never
    {
        throw new ServiceError(503, 'unavailable', 'Community storage is unavailable.');
    }

    private static function reject(string $message, int $status = 400, string $code = 'bad_content'): never
    {
        throw new ServiceError($status, $code, $message);
    }

    private static function digest(string $value): bool
    {
        return strlen($value) === 64 && strspn($value, '0123456789abcdef') === 64;
    }

    private static function integer(string $text, int $max): int
    {
        if (!preg_match('/^(0|[1-9][0-9]{0,10})$/D', $text) || (int)$text > $max)
            self::reject('A content number is invalid.');
        return (int)$text;
    }

    private static function decode(string $hex, int $max, bool $empty = false): string
    {
        if ($hex === '' && $empty) return '';
        if (strlen($hex) > $max * 2 || !Http::isHex($hex)) self::reject('Content encoding is invalid.');
        return (string)hex2bin($hex);
    }

    private static function portablePath(string $path): bool
    {
        if ($path === '' || strlen($path) > 240 || preg_match('/[^\x20-\x7e]/', $path)
            || strpbrk($path, '\\:*?"<>|') !== false) return false;
        foreach (explode('/', $path) as $part) {
            if ($part === '' || $part === '.' || $part === '..' || trim($part) !== $part
                || str_ends_with($part, '.') || preg_match('/^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$)/iD', $part)) return false;
        }
        return true;
    }

    /** Strict canonical byte representation: changing it changes the immutable revision hash. */
    public static function parseManifest(string $text): array
    {
        if (strlen($text) > self::MAX_MANIFEST || !str_ends_with($text, "\n")
            || preg_match('/[^\x20-\x7e\n]/', $text)) self::reject('The content manifest is invalid.');
        $lines = explode("\n", substr($text, 0, -1));
        if (count($lines) < 7 || count($lines) > 6 + self::MAX_FILES || array_shift($lines) !== 'DUNEWORKSHOP1')
            self::reject('The content manifest is invalid.');
        $out = [];
        foreach (['kind', 'id', 'name', 'base', 'mod'] as $key) {
            $line = array_shift($lines);
            if (!str_starts_with($line, $key . '=')) self::reject('The manifest fields are not canonical.');
            $out[$key] = substr($line, strlen($key) + 1);
        }
        if (!in_array($out['kind'], ['map', 'mod'], true)
            || !preg_match('/^[0-9a-f]{32}$/D', $out['id'])) self::reject('The content identity is invalid.');
        $name = self::decode($out['name'], 128);
        if (!preg_match('//u', $name) || preg_match('/[\x00-\x1f\x7f]/', $name) || trim($name) === '')
            self::reject('The content name is invalid.');
        $base = self::decode($out['base'], 64, true);
        if ($base !== '' && (!self::portablePath($base) || str_contains($base, '/')))
            self::reject('The base mod name is invalid.');
        if (($out['mod'] !== '' && !self::digest($out['mod'])) || ($out['kind'] === 'mod' && $out['mod'] !== ''))
            self::reject('The mod dependency is invalid.');
        $out['files'] = [];
        $out['total'] = 0;
        $previous = '';
        $seen = [];
        foreach ($lines as $line) {
            if (!preg_match('/^file=([0-9a-f]{64}),(0|[1-9][0-9]{0,9}),([0-9a-f]+)$/D', $line, $m))
                self::reject('A manifest file is invalid.');
            $path = self::decode($m[3], 240);
            if (!self::portablePath($path) || strcmp($previous, $path) >= 0 || isset($seen[strtolower($path)]))
                self::reject('Manifest paths must be safe, unique and sorted.');
            // A file cannot also be a directory, including differently-cased directory aliases.
            $fold = strtolower($path);
            $previous = $path;
            $seen[$fold] = true;
            $size = self::integer($m[2], self::MAX_FILE);
            $out['total'] += $size;
            if ($out['total'] > self::MAX_TOTAL) self::reject('This content package is too large.', 413);
            $out['files'][] = ['hash' => $m[1], 'size' => $size, 'path' => $path];
        }
        $directories = [];
        foreach ($out['files'] as $file) {
            $originalParts = explode('/', $file['path']);
            array_pop($originalParts);
            while ($originalParts !== []) {
                $directory = implode('/', $originalParts);
                $key = strtolower($directory);
                if (isset($directories[$key]) && $directories[$key] !== $directory)
                    self::reject('Directory names must use consistent letter case.');
                $directories[$key] = $directory;
                array_pop($originalParts);
            }
            $parts = explode('/', strtolower($file['path']));
            array_pop($parts);
            while ($parts !== []) {
                if (isset($seen[implode('/', $parts)])) self::reject('A manifest path is both a file and directory.');
                array_pop($parts);
            }
        }
        if (count($out['files']) < 1 || count($out['files']) > self::MAX_FILES) self::reject('The package file count is invalid.');
        if ($out['kind'] === 'map' && (count($out['files']) !== 1 || $out['files'][0]['path'] !== 'map.ini'
            || $out['files'][0]['size'] > 1048576)) self::reject('A map must contain only map.ini, up to 1 MiB.');
        if ($out['kind'] === 'mod' && !in_array('mod.ini', array_column($out['files'], 'path'), true))
            self::reject('A mod must contain mod.ini metadata.');
        $sizes = [];
        foreach ($out['files'] as $file) {
            if (isset($sizes[$file['hash']]) && $sizes[$file['hash']] !== $file['size'])
                self::reject('A file hash has inconsistent sizes.');
            $sizes[$file['hash']] = $file['size'];
        }
        return $out;
    }

    private static function checkFile(string $path): void
    {
        $stat = @lstat($path);
        if (is_link($path) || $stat === false || ($stat['mode'] & 0170000) !== 0100000
            || ($stat['mode'] & 077) !== 0
            || (function_exists('posix_geteuid') && $stat['uid'] !== posix_geteuid())) self::unavailable();
    }

    /** Verify the handle and pathname still identify the same private regular file. */
    private static function opened(string $path, string $mode)
    {
        self::checkFile($path);
        $fp = @fopen($path, $mode);
        if ($fp === false) self::unavailable();
        $opened = fstat($fp);
        $named = lstat($path);
        if ($opened === false || $named === false || ($opened['mode'] & 0170000) !== 0100000
            || ($opened['mode'] & 077) !== 0 || $opened['ino'] !== $named['ino']
            || $opened['dev'] !== $named['dev']
            || (function_exists('posix_geteuid') && $opened['uid'] !== posix_geteuid())) {
            fclose($fp);
            self::unavailable();
        }
        return $fp;
    }

    private static function read(string $path, int $maximum): string
    {
        $fp = self::opened($path, 'rb');
        try {
            $bytes = stream_get_contents($fp, $maximum + 1);
            if ($bytes === false || strlen($bytes) > $maximum) self::unavailable();
            return $bytes;
        } finally { fclose($fp); }
    }

    private static function atomic(string $path, string $bytes): void
    {
        $temp = $path . '.' . bin2hex(random_bytes(8)) . '.tmp';
        $fp = @fopen($temp, 'xb');
        if ($fp === false) self::unavailable();
        @chmod($temp, 0600);
        try {
            $offset = 0;
            while ($offset < strlen($bytes)) {
                $n = fwrite($fp, substr($bytes, $offset));
                if ($n === false || $n === 0) self::unavailable();
                $offset += $n;
            }
            if (!fflush($fp)) self::unavailable();
            if (function_exists('fsync') && !fsync($fp)) self::unavailable();
        } finally { fclose($fp); }
        if (!@rename($temp, $path)) { @unlink($temp); self::unavailable(); }
    }

    /** @return list<array{0:string,1:string}> */
    public function handle(string $action, array $form, string $address): array
    {
        $path = $this->dir . '/index.lock';
        if (!file_exists($path) && !is_link($path)) {
            $new = @fopen($path, 'xb');
            if ($new !== false) { @chmod($path, 0600); fclose($new); }
        }
        $lock = self::opened($path, 'r+b');
        if ($lock === false || !flock($lock, LOCK_EX)) self::unavailable();
        try {
            $statePath = $this->dir . '/index.json';
            $state = ['items' => [], 'revisions' => [], 'uploads' => [], 'bytes' => 0, 'maps' => []];
            if (file_exists($statePath) || is_link($statePath)) {
                self::checkFile($statePath);
                if (filesize($statePath) > 16 * 1024 * 1024) self::unavailable();
                $state = json_decode(self::read($statePath, 16 * 1024 * 1024), true, 512, JSON_THROW_ON_ERROR);
                if (!is_array($state) || !isset($state['items'], $state['revisions'], $state['uploads'], $state['bytes'])) self::unavailable();
            }
            // Map metadata is a derived cache an older index simply does not carry yet.
            if (!isset($state['maps']) || !is_array($state['maps'])) $state['maps'] = [];
            $before = $state;
            $this->expire($state);
            if ($state !== $before) {
                self::atomic($statePath, json_encode($state, JSON_THROW_ON_ERROR));
                $before = $state;
            }
            $result = match ($action) {
                'begin' => $this->begin($state, $form, $address),
                'chunk' => $this->chunk($state, $form),
                'commit' => $this->commit($state, $form),
                'list' => $this->listing($state, $form),
                'manifest' => $this->manifest($state, $form),
                'blob' => $this->blob($state, $form),
                default => throw new ServiceError(404, 'bad_request', 'Unknown content endpoint.'),
            };
            if ($state !== $before) self::atomic($statePath, json_encode($state, JSON_THROW_ON_ERROR));
            return array_merge([['status', 'ok']], $result);
        } finally { flock($lock, LOCK_UN); fclose($lock); }
    }

    private function expire(array &$state): void
    {
        foreach ($state['uploads'] as $token => $upload) {
            if ($upload['expires'] >= time()) continue;
            $dir = $this->dir . '/uploads/' . $token;
            if (is_dir($dir) && !is_link($dir)) {
                foreach (glob($dir . '/*') ?: [] as $file) @unlink($file);
                @rmdir($dir);
            }
            unset($state['uploads'][$token]);
        }
    }

    private function begin(array &$state, array $form, string $address): array
    {
        $raw = self::decode($form['manifest'] ?? '', self::MAX_MANIFEST);
        $hash = $form['hash'] ?? '';
        if (!self::digest($hash) || !hash_equals(hash('sha256', $raw), $hash)) self::reject('The manifest checksum does not match.');
        $meta = self::parseManifest($raw);
        if (isset($state['revisions'][$hash])) return [['version', (string)$state['revisions'][$hash]['version']], ['hash', $hash]];
        $owner = $form['owner'] ?? '';
        if (!self::digest($owner)) self::reject('An owner capability is required.', 403, 'owner_required');
        $owner = hash('sha256', $owner);
        $item = $state['items'][$meta['id']] ?? null;
        if ($item !== null && (!hash_equals($item['owner'], $owner) || $item['kind'] !== $meta['kind']))
            self::reject('This item belongs to another creator. Save a copy to share your changes.', 403, 'not_owner');
        $reserved = 0;
        $active = 0;
        foreach ($state['uploads'] as $token => $upload) {
            if ($upload['hash'] === $hash && hash_equals($upload['owner'], $owner)) return [['upload', $token]];
            $reserved += $upload['total']; // Includes retained staging for commit retries.
            if (!isset($upload['version'])) {
                if ($upload['address'] === hash('sha256', $address)) ++$active;
            }
        }
        // Reconcile actual immutable storage after a process interruption between blob and
        // index commits. Such orphan blobs still consume quota and must not reset accounting.
        $storedBytes = 0;
        $storedFiles = 0;
        foreach (new DirectoryIterator($this->dir . '/blobs') as $entry) {
            if ($entry->isDot()) continue;
            if (str_ends_with($entry->getFilename(), '.tmp')) { @unlink($entry->getPathname()); continue; }
            self::checkFile($entry->getPathname());
            $storedBytes += $entry->getSize();
            if (++$storedFiles > 65536) self::reject('Community file storage is full.', 429, 'quota_exceeded');
        }
        $state['bytes'] = $storedBytes;
        if ($storedFiles + count($meta['files']) > 65536 || count($state['uploads']) >= 256 || $active >= 8 || count($state['revisions']) >= 10000
            || $state['bytes'] + $reserved + $meta['total'] > $this->config->get('content_quota_bytes'))
            self::reject('Community storage is full. Try again later.', 429, 'quota_exceeded');
        if ($meta['mod'] !== '' && ($state['revisions'][$meta['mod']]['kind'] ?? '') !== 'mod')
            self::reject('Share the exact required mod version before sharing this map.', 409, 'missing_dependency');
        $token = bin2hex(random_bytes(32));
        $dir = $this->dir . '/uploads/' . $token;
        if (!mkdir($dir, 0700)) self::unavailable();
        self::atomic($dir . '/manifest', $raw);
        $state['uploads'][$token] = ['hash' => $hash, 'owner' => $owner, 'address' => hash('sha256', $address),
            'total' => $meta['total'], 'expires' => time() + self::UPLOAD_TTL,
            'promoted' => ($form['promoted'] ?? '1') === '1', 'source' => ($form['source'] ?? 'manual') === 'host' ? 'host' : 'manual'];
        return [['upload', $token]];
    }

    private function upload(array $state, array $form): array
    {
        $token = $form['upload'] ?? '';
        if (!self::digest($token) || !isset($state['uploads'][$token])) self::reject('The upload has expired. Start sharing again.', 404, 'missing_upload');
        $upload = $state['uploads'][$token];
        $dir = $this->dir . '/uploads/' . $token;
        if (is_link($dir) || realpath($dir) !== $dir) self::unavailable();
        self::checkFile($dir . '/manifest');
        return [$token, $upload, $dir, self::parseManifest(self::read($dir . '/manifest', self::MAX_MANIFEST))];
    }

    private static function fileSpec(array $meta, string $hash): array
    {
        if (!self::digest($hash)) self::reject('The file checksum is invalid.');
        foreach ($meta['files'] as $file) if ($file['hash'] === $hash) return $file;
        self::reject('That file is not part of this revision.', 404, 'missing_file');
    }

    private function chunk(array &$state, array $form): array
    {
        [$token, $upload, $dir, $meta] = $this->upload($state, $form);
        $file = self::fileSpec($meta, $form['file'] ?? '');
        $data = self::decode($form['data'] ?? '', self::CHUNK, true);
        $offset = self::integer($form['offset'] ?? '', self::MAX_FILE);
        if ($offset + strlen($data) > $file['size']) self::reject('The uploaded file is too long.');
        $path = $dir . '/' . $file['hash'];
        if (isset($upload['version'])) $path = $this->dir . '/blobs/' . $file['hash'];
        if (!file_exists($path) && !is_link($path)) {
            $fp = @fopen($path, 'xb');
            if ($fp === false) self::unavailable();
            @chmod($path, 0600); fclose($fp);
        }
        self::checkFile($path);
        clearstatcache(true, $path);
        $size = filesize($path);
        if ($offset > $size || ($offset < $size && $offset + strlen($data) > $size))
            self::reject('Upload chunks must be sent in order.', 409, 'wrong_offset');
        $fp = self::opened($path, isset($upload['version']) ? 'rb' : 'r+b');
        if ($fp === false) self::unavailable();
        try {
            if (fseek($fp, $offset) !== 0) self::unavailable();
            if ($offset < $size || isset($upload['version'])) {
                $existing = strlen($data) ? fread($fp, strlen($data)) : '';
                if ($existing !== $data) self::reject('A repeated upload chunk does not match.', 409, 'chunk_mismatch');
            } else {
                if (strlen($data) && fwrite($fp, $data) !== strlen($data)) self::unavailable();
                if (!fflush($fp)) self::unavailable();
                $size += strlen($data);
            }
        } finally { fclose($fp); }
        $state['uploads'][$token]['expires'] = time() + self::UPLOAD_TTL;
        return [['next', (string)$size]];
    }

    private function commit(array &$state, array $form): array
    {
        [$token, $upload, $dir, $meta] = $this->upload($state, $form);
        $hash = $upload['hash'];
        if (isset($state['revisions'][$hash])) {
            $state['uploads'][$token]['version'] = $state['revisions'][$hash]['version'];
            return [['version', (string)$state['revisions'][$hash]['version']], ['hash', $hash]];
        }
        $item = $state['items'][$meta['id']] ?? null;
        if ($item !== null && (!hash_equals($item['owner'], $upload['owner']) || $item['kind'] !== $meta['kind']))
            self::reject('This item belongs to another creator. Save a copy to share your changes.', 403, 'not_owner');
        if ($meta['mod'] !== '' && ($state['revisions'][$meta['mod']]['kind'] ?? '') !== 'mod')
            self::reject('The required mod revision is unavailable.', 409, 'missing_dependency');
        $unique = [];
        foreach ($meta['files'] as $file) {
            if (isset($unique[$file['hash']])) continue;
            $unique[$file['hash']] = $file;
            $path = $dir . '/' . $file['hash'];
            if (!file_exists($path)) {
                if ($file['size'] === 0) self::atomic($path, '');
                else self::reject('The upload is incomplete.', 409, 'incomplete_upload');
            }
            self::checkFile($path);
            clearstatcache(true, $path);
            if (filesize($path) !== $file['size']) self::reject('The upload is incomplete.', 409, 'incomplete_upload');
            if (!hash_equals($file['hash'], (string)hash_file('sha256', $path)))
                self::reject('An uploaded file checksum does not match.', 409, 'checksum_mismatch');
        }
        // Verify the entire snapshot before making any new immutable blob visible.
        foreach ($unique as $file) {
            $blob = $this->dir . '/blobs/' . $file['hash'];
            if (file_exists($blob) || is_link($blob)) {
                self::checkFile($blob);
                if (filesize($blob) !== $file['size'] || hash_file('sha256', $blob) !== $file['hash']) self::unavailable();
            } else {
                // Publish a complete blob atomically; an interrupted copy is never a blob.
                $temp = $blob . '.' . bin2hex(random_bytes(8)) . '.tmp';
                if (!copy($dir . '/' . $file['hash'], $temp)) { @unlink($temp); self::unavailable(); }
                chmod($temp, 0600);
                $fp = fopen($temp, 'r+b');
                if ($fp === false) self::unavailable();
                try { if (function_exists('fsync') && !fsync($fp)) self::unavailable(); }
                finally { fclose($fp); }
                if (!rename($temp, $blob)) { @unlink($temp); self::unavailable(); }
                $state['bytes'] += $file['size'];
            }
        }
        self::atomic($this->dir . '/manifests/' . $hash, self::read($dir . '/manifest', self::MAX_MANIFEST));
        $version = ($item['version'] ?? 0) + 1;
        $state['items'][$meta['id']] = ['owner' => $upload['owner'], 'kind' => $meta['kind'], 'version' => $version];
        $state['revisions'][$hash] = ['id' => $meta['id'], 'kind' => $meta['kind'], 'name' => $meta['name'],
            'base' => $meta['base'], 'mod' => $meta['mod'], 'version' => $version,
            'promoted' => $upload['promoted'], 'source' => $upload['source']];
        $state['uploads'][$token]['version'] = $version;
        if ($meta['kind'] === 'map') $this->captureMap($state, $hash, $meta);
        return [['version', (string)$version], ['hash', $hash]];
    }

    /** Keep a browsable maps folder plus derived catalogue metadata for every committed map.
     * The entry is a hard link to the already verified immutable blob, named by the server-side
     * revision hash, so no uploaded name reaches the filesystem and no unaccounted bytes are
     * stored: quota still counts exactly one copy. A metadata failure never invalidates a
     * revision that is already immutably stored.
     */
    private function captureMap(array &$state, string $hash, array $meta): void
    {
        try {
            if ($meta['files'][0]['size'] > self::MAX_MAP_INI) return;
            $text = $this->linkMap($hash, $meta['files'][0]['hash']);
            $map = self::parseMapIni($text);
            $map['file'] = $meta['files'][0]['hash'];
            $state['maps'][$hash] = $map;
            $this->writeMapMetadata($state, $hash, $map);
        } catch (Throwable) {
            // Leave the revision uncached; the catalogue retries the bootstrap lazily.
        }
    }

    /** Link maps/<revision>.ini at the verified blob and return the map text. */
    private function linkMap(string $hash, string $file): string
    {
        if (!self::digest($hash) || !self::digest($file)) self::unavailable();
        $path = $this->dir . '/maps/' . $hash . '.ini';
        $blob = $this->dir . '/blobs/' . $file;
        self::checkFile($blob);
        $text = self::read($blob, self::MAX_MAP_INI);
        if (!file_exists($path) && !is_link($path)) {
            // A filesystem without links simply has no maps folder entry; nothing is duplicated.
            if (@link($blob, $path)) self::checkFile($path);
        }
        return $text;
    }

    /** Release tooling can use this sidecar without interpreting the private revision index. */
    private function writeMapMetadata(array $state, string $hash, array $map): void
    {
        $row = $state['revisions'][$hash];
        self::atomic($this->dir . '/maps/' . $hash . '.json', json_encode([
            'name' => hex2bin($row['name']), 'id' => $row['id'], 'version' => $row['version'],
            'mod' => self::modIdentity($state, $row, $map), 'mod_revision' => $row['mod'],
            'width' => $map['width'], 'height' => $map['height'], 'max_players' => $map['players'],
            'revision' => $hash, 'file_sha256' => $map['file'], 'source' => $row['source'],
        ], JSON_THROW_ON_ERROR | JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE));
    }

    private static function unknownMap(): array
    {
        return ['schema' => 3, 'width' => 0, 'height' => 0, 'players' => 0, 'mod' => 'vanilla', 'known' => false, 'file' => ''];
    }

    private static function iniInt(string $value, int $fallback): int
    {
        $value = trim($value);
        return preg_match('/^-?[0-9]{1,6}$/D', $value) ? (int)$value : $fallback;
    }

    /** Bounded reader for the map format the client itself parses: sections and `key=value`,
     * case-insensitive, comments ignored. Anything unrecognised leaves a zero (unknown) field
     * rather than a guess. `[BASIC] Version` is the map *format* version and is deliberately not
     * read here: content versions are the numbers this server assigns.
     */
    public static function parseMapIni(string $text): array
    {
        $out = self::unknownMap();
        if (strlen($text) > self::MAX_MAP_INI) return $out;
        $out['known'] = true;
        $section = '';
        $sections = [];
        $map = [];
        $basic = [];
        $lines = 0;
        foreach (explode("\n", $text) as $line) {
            if (++$lines > self::MAX_MAP_LINES) break;
            $line = trim($line, " \t\r\0\x0b");
            if ($line === '' || $line[0] === ';' || $line[0] === '#') continue;
            if ($line[0] === '[') {
                $end = strpos($line, ']');
                $section = $end === false ? '' : strtolower(trim(substr($line, 1, $end - 1)));
                if ($section !== '' && count($sections) < 1024) $sections[$section] = true;
                continue;
            }
            $split = strpos($line, '=');
            if ($split === false || $split === 0) continue;
            $key = strtolower(trim(substr($line, 0, $split)));
            $value = trim(substr($line, $split + 1));
            if (strlen($value) > 256) $value = substr($value, 0, 256);
            if ($value !== '' && $value[0] === '"' && str_ends_with($value, '"') && strlen($value) > 1)
                $value = substr($value, 1, -1);
            if ($section === 'map' && in_array($key, ['sizex', 'sizey', 'seed'], true)) $map[$key] = $value;
            elseif ($section === 'basic' && $key === 'mapscale') $basic[$key] = $value;
            elseif ($section === 'structures' && preg_match('/^(id|gen)[0-9]+$/D', $key)) {
                $parts = explode(',', $value);
                $building = strtolower(trim($parts[1] ?? ''));
                if (in_array($building, self::CITY_BUILDINGS, true)) $out['mod'] = 'dunecity';
                elseif ($out['mod'] !== 'dunecity' && in_array($building, self::TORNIE_BUILDINGS, true)) $out['mod'] = 'tornie';
            }
        }
        if (isset($map['seed'])) {
            // Legacy seed maps carry no dimensions; the scale decides them, as in CustomGameMenu.
            [$width, $height] = match (self::iniInt($basic['mapscale'] ?? '', -1)) {
                0 => [62, 62], 1 => [32, 32], 2 => [21, 21], default => [64, 64],
            };
        } else {
            $width = self::iniInt($map['sizex'] ?? '', 0);
            $height = self::iniInt($map['sizey'] ?? '', 0);
            if ($width < 1 || $height < 1 || $width > self::MAX_MAP_SIDE || $height > self::MAX_MAP_SIDE)
                { $width = 0; $height = 0; }
        }
        $out['width'] = $width;
        $out['height'] = $height;
        foreach (self::PLAYER_SECTIONS as $name) if (isset($sections[$name])) ++$out['players'];
        return $out;
    }

    /** Metadata for one committed map revision, bootstrapping older revisions on first browse. */
    private function mapMetadata(array &$state, string $hash, array &$budget): array
    {
        $cached = $state['maps'][$hash] ?? null;
        if (is_array($cached) && ($cached['schema'] ?? 0) === 3 && isset($cached['width'], $cached['height'], $cached['players'],
            $cached['mod'], $cached['known'], $cached['file'])) return $cached;
        if ($budget['files'] <= 0 || $budget['bytes'] <= 0) return self::unknownMap();
        --$budget['files'];
        try {
            $meta = self::parseManifest(self::read($this->dir . '/manifests/' . $hash, self::MAX_MANIFEST));
            if ($meta['kind'] !== 'map' || $meta['files'][0]['size'] > self::MAX_MAP_INI) return self::unknownMap();
            $text = $this->linkMap($hash, $meta['files'][0]['hash']);
            $budget['bytes'] -= strlen($text);
            $map = self::parseMapIni($text);
            $map['file'] = $meta['files'][0]['hash'];
            $state['maps'][$hash] = $map;
            $this->writeMapMetadata($state, $hash, $map);
            return $map;
        } catch (Throwable) {
            // One unreadable legacy revision must never hide the rest of the catalogue.
            return self::unknownMap();
        }
    }

    /** Catalogue category is derived from map buildings, independently of the
     * immutable gameplay dependency. Old tags and active mod names are not evidence. */
    private static function modIdentity(array $state, array $row, array $meta): string
    {
        return $meta['mod'] ?: 'vanilla';
    }

    private function listing(array &$state, array $form): array
    {
        $catalogue = $form['catalogue'] ?? '';
        if ($catalogue === 'maps') return $this->mapCatalogue($state, $form);
        if ($catalogue !== '') self::reject('Unknown catalogue.');
        $kind = $form['kind'] ?? '';
        if (!in_array($kind, ['', 'map', 'mod'], true)) self::reject('Unknown content type.');
        $cursor = self::integer($form['cursor'] ?? '0', 10000);
        $rows = array_filter($state['revisions'], static fn(array $row): bool => $kind === '' || $kind === $row['kind']);
        $page = array_slice($rows, $cursor, 50, true);
        $out = [];
        foreach ($page as $hash => $row) $out[] = ['item', implode(',', [$row['kind'], $row['id'],
            $row['version'], $hash, $row['name'], $row['base'], $row['mod']])];
        $out[] = ['next', (string)($cursor + count($page) < count($rows) ? $cursor + count($page) : 0)];
        return $out;
    }

    /** The browsable map catalogue: one row per stable item, always its newest revision, with the
     * metadata a filter needs. Automatic host uploads are ordinary revisions and are included.
     */
    private function mapCatalogue(array &$state, array $form): array
    {
        $kind = $form['kind'] ?? '';
        if (!in_array($kind, ['', 'map'], true)) self::reject('Unknown content type.');
        $cursor = self::integer($form['cursor'] ?? '0', 10000);
        // An absent or empty mod field browses every mod, including the base game.
        $mod = (string)($form['mod'] ?? '');
        if ($mod !== '' && $mod !== self::MOD_UNKNOWN && $mod !== self::MOD_BASE_GAME
            && (strlen($mod) > 64 || !self::portablePath($mod) || str_contains($mod, '/')))
            self::reject('The mod filter is invalid.');
        $size = $form['size'] ?? '';
        $width = 0;
        $height = 0;
        if ($size !== '') {
            if (!preg_match('/^([1-9][0-9]{0,3})x([1-9][0-9]{0,3})$/D', $size, $m)
                || (int)$m[1] > self::MAX_MAP_SIDE || (int)$m[2] > self::MAX_MAP_SIDE)
                self::reject('The size filter must be WIDTHxHEIGHT.');
            $width = (int)$m[1];
            $height = (int)$m[2];
        }
        $players = $form['players'] ?? '';
        if ($players !== '' && !preg_match('/^([1-9]|1[0-2])$/D', $players))
            self::reject('The player filter must be a player count.');
        $wanted = $players === '' ? 0 : (int)$players;

        $latest = [];
        foreach ($state['revisions'] as $hash => $row) {
            if ($row['kind'] !== 'map') continue;
            $known = $latest[$row['id']] ?? null;
            if ($known === null || $row['version'] > $known['row']['version'])
                $latest[$row['id']] = ['hash' => $hash, 'row' => $row];
        }

        // Bootstrapping older revisions reads at most this much per request; whatever is left is
        // cached by the next page request instead.
        $budget = ['files' => 64, 'bytes' => 16 * 1024 * 1024];
        $rows = [];
        $matched = 0;
        $more = false;
        $seen = [];
        foreach ($latest as $entry) {
            $meta = $this->mapMetadata($state, $entry['hash'], $budget);
            $identity = self::modIdentity($state, $entry['row'], $meta);
            // Keep distinct exact gameplay dependencies even when their building category matches.
            if ($meta['file'] !== '') {
                $key = $meta['file'] . '|' . strtolower($identity) . '|' . $entry['row']['mod'];
                if (isset($seen[$key])) continue;
                $seen[$key] = true;
            }
            if ($mod !== '' && strcasecmp($mod === self::MOD_BASE_GAME ? 'vanilla' : $mod, $identity) !== 0) continue;
            if ($width !== 0 && ($meta['width'] !== $width || $meta['height'] !== $height)) continue;
            if ($wanted !== 0 && $meta['players'] !== $wanted) continue;
            ++$matched;
            if ($matched <= $cursor) continue;
            if (count($rows) >= 50) { $more = true; break; }
            $rows[] = ['item', implode(',', [$entry['row']['kind'], $entry['row']['id'], $entry['row']['version'],
                $entry['hash'], $entry['row']['name'], $entry['row']['base'], $entry['row']['mod'],
                bin2hex($identity), $meta['width'], $meta['height'], $meta['players']])];
        }
        $next = $more ? $cursor + count($rows) : 0;
        $rows[] = ['next', (string)$next];
        return $rows;
    }

    private function revision(array $state, array $form): array
    {
        $hash = $form['hash'] ?? '';
        if (!self::digest($hash) || !isset($state['revisions'][$hash])) self::reject('This content revision was not found.', 404, 'missing_revision');
        $path = $this->dir . '/manifests/' . $hash;
        self::checkFile($path);
        $raw = self::read($path, self::MAX_MANIFEST);
        if (!hash_equals($hash, hash('sha256', $raw))) self::unavailable();
        return [$hash, $state['revisions'][$hash], $raw];
    }

    private function manifest(array $state, array $form): array
    {
        [, $revision, $raw] = $this->revision($state, $form);
        return [['version', (string)$revision['version']], ['manifest', bin2hex($raw)]];
    }

    private function blob(array $state, array $form): array
    {
        [, , $raw] = $this->revision($state, $form);
        $file = self::fileSpec(self::parseManifest($raw), $form['file'] ?? '');
        $offset = self::integer($form['offset'] ?? '', self::MAX_FILE);
        $count = self::integer($form['count'] ?? (string)self::CHUNK, self::CHUNK);
        if ($offset > $file['size'] || $count === 0) self::reject('The download range is invalid.');
        $path = $this->dir . '/blobs/' . $file['hash'];
        self::checkFile($path);
        $fp = self::opened($path, 'rb');
        if ($fp === false) self::unavailable();
        try {
            if (fseek($fp, $offset) !== 0) self::unavailable();
            $bytes = fread($fp, min($count, max(1, $file['size'] - $offset)));
            if ($bytes === false || strlen($bytes) !== min($count, $file['size'] - $offset)) self::unavailable();
        } finally { fclose($fp); }
        return [['data', bin2hex($bytes)]];
    }

    public static function send(Http $http, int $status, array $lines): void
    {
        $body = '';
        foreach ($lines as [$key, $value]) {
            if (!preg_match('/^[a-z]{1,16}$/D', $key) || preg_match('/[^\x20-\x7e]/', $value))
                throw new LogicException('Invalid content response');
            $body .= $key . '=' . $value . "\n";
        }
        if (strlen($body) > 524288) throw new LogicException('Oversized content response');
        http_response_code($status);
        foreach ($http->corsHeaders() as $key => $value) header($key . ': ' . $value);
        header('Content-Type: text/plain; charset=utf-8');
        header('Content-Length: ' . strlen($body));
        header('Cache-Control: no-store');
        header('X-Content-Type-Options: nosniff');
        echo $body;
    }
}
