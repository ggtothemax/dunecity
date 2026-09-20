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
    private string $dir;

    public function __construct(private readonly Config $config)
    {
        // The ingress Rate/Store call has already bootstrapped and verified the private parent.
        $this->dir = $config->stateDir() . '/content';
        foreach ([$this->dir, $this->dir . '/blobs', $this->dir . '/manifests', $this->dir . '/uploads'] as $dir) {
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
            $state = ['items' => [], 'revisions' => [], 'uploads' => [], 'bytes' => 0];
            if (file_exists($statePath) || is_link($statePath)) {
                self::checkFile($statePath);
                if (filesize($statePath) > 16 * 1024 * 1024) self::unavailable();
                $state = json_decode(self::read($statePath, 16 * 1024 * 1024), true, 512, JSON_THROW_ON_ERROR);
                if (!is_array($state) || !isset($state['items'], $state['revisions'], $state['uploads'], $state['bytes'])) self::unavailable();
            }
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
        return [['version', (string)$version], ['hash', $hash]];
    }

    private function listing(array $state, array $form): array
    {
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
