<?php
/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

/**
 * Tests for the pure-PHP call credentials path (GRPC_PHP_PURE_CALL_CREDENTIALS=1).
 *
 * Each test that toggles the experiment flag resets the cached static value via
 * reflection so subsequent tests start clean.
 *
 * Structure:
 *  - Unit tests (no network): validate _applyCallCredentials behaviour directly.
 *  - Integration tests (real server): verify metadata reaches the server end-to-end
 *    using ClientStreamingCall, which sends only OP_SEND_INITIAL_METADATA and
 *    requires no protobuf serialisation.
 */
class CallCredentialsPurePhpTest extends \PHPUnit\Framework\TestCase
{
    // Integration-test fixtures
    private $server;
    private $port;
    private $host_override;
    private $ssl_channel;

    // Reflection helper to reset the static experiment caches between tests.
    // Both AbstractCall and BaseStub cache the env var; both must be cleared.
    private static function resetExperimentCache(): void
    {
        $p1 = new \ReflectionProperty(\Grpc\AbstractCall::class, 'pure_call_credentials_enabled');
        $p1->setValue(null, null);

        $p2 = new \ReflectionProperty(\Grpc\BaseStub::class, 'pure_call_credentials_enabled');
        $p2->setValue(null, null);
    }

    private static function enableExperiment(): void
    {
        putenv('GRPC_PHP_PURE_CALL_CREDENTIALS=1');
        self::resetExperimentCache();
    }

    private static function disableExperiment(): void
    {
        putenv('GRPC_PHP_PURE_CALL_CREDENTIALS=');
        self::resetExperimentCache();
    }

    public function setUp(): void
    {
        // Start a real TLS server so integration tests can verify wire metadata.
        $server_credentials = Grpc\ServerCredentials::createSsl(
            null,
            file_get_contents(dirname(__FILE__).'/../data/server1.key'),
            file_get_contents(dirname(__FILE__).'/../data/server1.pem')
        );
        $this->server = new Grpc\Server();
        $this->port = $this->server->addSecureHttp2Port('0.0.0.0:0', $server_credentials);
        $this->server->start();

        $this->host_override = 'foo.test.google.fr';
        $ssl_creds = Grpc\ChannelCredentials::createSsl(
            file_get_contents(dirname(__FILE__).'/../data/ca.pem')
        );
        $this->ssl_channel = new Grpc\Channel(
            'localhost:'.$this->port,
            [
                'force_new' => true,
                'grpc.ssl_target_name_override' => $this->host_override,
                'grpc.default_authority'         => $this->host_override,
                'credentials'                    => $ssl_creds,
            ]
        );
    }

    public function tearDown(): void
    {
        self::disableExperiment();
        unset($this->ssl_channel);
        unset($this->server);
    }

    // -------------------------------------------------------------------------
    // Unit tests — exercise _applyCallCredentials through a concrete subclass
    // -------------------------------------------------------------------------

    /**
     * Expose the protected _applyCallCredentials for direct unit testing.
     */
    private function makeCall(Grpc\Channel $channel, ?callable $cb = null): Grpc\ClientStreamingCall
    {
        $options = [];
        if ($cb !== null) {
            $options['call_credentials_callback'] = $cb;
        }
        return new Grpc\ClientStreamingCall($channel, '/pkg.Svc/Method', [null, null], $options);
    }

    public function testLegacyPathStoresNothingWhenExperimentOff(): void
    {
        self::disableExperiment();
        $cb = function ($ctx) { return ['x-token' => ['val']]; };
        $call = $this->makeCall($this->ssl_channel, $cb);

        // In legacy mode the callback is NOT stored on the PHP object.
        $prop = new \ReflectionProperty(\Grpc\AbstractCall::class, 'call_credentials_callback');
        
        $this->assertNull($prop->getValue($call));
    }

    public function testPurePathStoresCallbackWhenExperimentOn(): void
    {
        self::enableExperiment();
        $cb = function ($ctx) { return ['x-token' => ['val']]; };
        $call = $this->makeCall($this->ssl_channel, $cb);

        $prop = new \ReflectionProperty(\Grpc\AbstractCall::class, 'call_credentials_callback');
        
        $this->assertSame($cb, $prop->getValue($call));
    }

    public function testApplyCallCredentialsMergesMetadata(): void
    {
        self::enableExperiment();
        $cb = function ($ctx) { return ['x-token' => ['tok1']]; };
        $call = $this->makeCall($this->ssl_channel, $cb);

        $method = new \ReflectionMethod(\Grpc\AbstractCall::class, '_applyCallCredentials');
        

        $result = $method->invoke($call, ['existing-key' => ['v']]);
        $this->assertArrayHasKey('existing-key', $result);
        $this->assertArrayHasKey('x-token', $result);
        $this->assertSame(['tok1'], $result['x-token']);
    }

    public function testApplyCallCredentialsNormalizesKeysToLowercase(): void
    {
        self::enableExperiment();
        // Callback returns an uppercase key — _applyCallCredentials lowercases it.
        $cb = function ($ctx) { return ['X-Token' => ['tok1']]; };
        $call = $this->makeCall($this->ssl_channel, $cb);

        $method = new \ReflectionMethod(\Grpc\AbstractCall::class, '_applyCallCredentials');
        

        $result = $method->invoke($call, []);
        $this->assertArrayHasKey('x-token', $result);
    }

    public function testApplyCallCredentialsRejectsNonArrayReturn(): void
    {
        self::enableExperiment();
        $cb = function ($ctx) { return 'not-an-array'; };
        $call = $this->makeCall($this->ssl_channel, $cb);

        $method = new \ReflectionMethod(\Grpc\AbstractCall::class, '_applyCallCredentials');
        

        $this->expectException(\InvalidArgumentException::class);
        $this->expectExceptionMessage('must return an array');
        $method->invoke($call, []);
    }

    public function testApplyCallCredentialsRejectsInvalidKeyChars(): void
    {
        self::enableExperiment();
        // Keys with characters outside [.a-z0-9_-] are rejected.
        $cb = function ($ctx) { return ['UPPER_CASE_NOT_ALLOWED!' => ['v']]; };
        $call = $this->makeCall($this->ssl_channel, $cb);

        $method = new \ReflectionMethod(\Grpc\AbstractCall::class, '_applyCallCredentials');
        

        $this->expectException(\InvalidArgumentException::class);
        $this->expectExceptionMessage('lowercase alphanumeric');
        $method->invoke($call, []);
    }

    public function testApplyCallCredentialsIsNoopWhenNoCallback(): void
    {
        self::enableExperiment();
        $call = $this->makeCall($this->ssl_channel);

        $method = new \ReflectionMethod(\Grpc\AbstractCall::class, '_applyCallCredentials');
        

        $metadata = ['key' => ['val']];
        $this->assertSame($metadata, $method->invoke($call, $metadata));
    }

    public function testContextContainsServiceUrlAndMethodName(): void
    {
        self::enableExperiment();
        $captured = null;
        $cb = function ($ctx) use (&$captured) {
            $captured = $ctx;
            return [];
        };
        $call = $this->makeCall($this->ssl_channel, $cb);

        $method = new \ReflectionMethod(\Grpc\AbstractCall::class, '_applyCallCredentials');
        
        $method->invoke($call, []);

        $this->assertNotNull($captured);
        $this->assertTrue(is_string($captured->service_url));
        $this->assertTrue(is_string($captured->method_name));
        $this->assertStringStartsWith('https://', $captured->service_url);
        $this->assertSame('/pkg.Svc/Method', $captured->method_name);
    }

    public function testSetCallCredentialsStoresCallableInPureMode(): void
    {
        self::enableExperiment();
        $call = $this->makeCall($this->ssl_channel);
        $cb = function ($ctx) { return ['x-new' => ['v']]; };
        $call->setCallCredentials($cb);

        $prop = new \ReflectionProperty(\Grpc\AbstractCall::class, 'call_credentials_callback');
        
        $this->assertSame($cb, $prop->getValue($call));
    }

    public function testSetCallCredentialsLegacyPathDoesNotStoreCallback(): void
    {
        self::disableExperiment();
        $call = $this->makeCall($this->ssl_channel);
        $cb = function ($ctx) { return ['x-new' => ['v']]; };

        // In legacy mode setCallCredentials delegates to the C extension.
        // It must NOT store the callable in the PHP property; verify the field
        // stays null regardless of what the C layer does with it.
        try {
            $call->setCallCredentials($cb);
        } catch (\Throwable $e) {
            // C extension may reject a bare callable — that's fine.
        }

        $prop = new \ReflectionProperty(\Grpc\AbstractCall::class, 'call_credentials_callback');
        $this->assertNull($prop->getValue($call));
    }

    public function testBuildServiceUrlFallbackFormat(): void
    {
        self::enableExperiment();
        $captured = null;
        $cb = function ($ctx) use (&$captured) {
            $captured = $ctx;
            return [];
        };
        // Construct directly — no _grpc_service_url injection, so _buildServiceUrl
        // fallback is used. Method is /SvcPkg.MySvc/MyMethod.
        $call = new Grpc\ClientStreamingCall(
            $this->ssl_channel,
            '/SvcPkg.MySvc/MyMethod',
            [null, null],
            ['call_credentials_callback' => $cb]
        );

        $method = new \ReflectionMethod(\Grpc\AbstractCall::class, '_applyCallCredentials');
        $method->invoke($call, []);

        $this->assertNotNull($captured);
        // service_url must include the service path but NOT the method name.
        $this->assertStringEndsWith('/SvcPkg.MySvc', $captured->service_url);
        $this->assertStringNotContainsString('MyMethod', $captured->service_url);
        $this->assertStringStartsWith('https://', $captured->service_url);
    }

    public function testInsecureChannelWithCallbackThrowsWhenExperimentOn(): void
    {
        self::enableExperiment();
        $this->expectException(\InvalidArgumentException::class);
        $this->expectExceptionMessage('insecure channel');

        // BaseStub detects the insecure channel and throws before the call starts.
        $stub = new class('localhost:1', [
            'credentials' => Grpc\ChannelCredentials::createInsecure(),
        ]) extends Grpc\BaseStub {
            public function callIt(array $options): void
            {
                $this->_clientStreamRequest('/pkg.Svc/Method', [null, null], [], $options);
            }
        };
        $stub->callIt(['call_credentials_callback' => function ($ctx) { return []; }]);
    }

    public function testInsecureChannelWithCallbackDoesNotThrowWhenExperimentOff(): void
    {
        self::disableExperiment();

        // Without the experiment flag the old C path is taken and no PHP-level
        // exception is raised for insecure + callback (gRPC core silently filters).
        // We only assert no InvalidArgumentException is thrown here; the C core
        // may still fail the RPC with a transport error, which is fine.
        $stub = new class('localhost:1', [
            'credentials' => Grpc\ChannelCredentials::createInsecure(),
        ]) extends Grpc\BaseStub {
            public function callIt(array $options): Grpc\ClientStreamingCall
            {
                return $this->_clientStreamRequest('/pkg.Svc/Method', [null, null], [], $options);
            }
        };

        $threw = false;
        try {
            $stub->callIt(['call_credentials_callback' => function ($ctx) { return []; }]);
        } catch (\InvalidArgumentException $e) {
            $threw = true;
        }
        $this->assertFalse($threw, 'Legacy path must not throw InvalidArgumentException');
    }

    // -------------------------------------------------------------------------
    // Integration test — verify metadata reaches the server via the pure PHP path
    // -------------------------------------------------------------------------

    public function callbackFunc($context)
    {
        $this->assertTrue(is_string($context->service_url));
        $this->assertTrue(is_string($context->method_name));
        return ['k1' => ['v1'], 'k2' => ['v2']];
    }

    public function testPurePhpPathInjectsMetadataEndToEnd(): void
    {
        self::enableExperiment();

        $deadline = Grpc\Timeval::infFuture();
        $call = new Grpc\ClientStreamingCall(
            $this->ssl_channel,
            '/abc/phony_method',
            [null, null],
            ['call_credentials_callback' => [$this, 'callbackFunc']]
        );

        // start() triggers _applyCallCredentials then OP_SEND_INITIAL_METADATA.
        $call->start([]);

        $event = $this->server->requestCall();
        $this->assertTrue(is_array($event->metadata));
        $metadata = $event->metadata;
        $this->assertArrayHasKey('k1', $metadata);
        $this->assertArrayHasKey('k2', $metadata);
        $this->assertSame(['v1'], $metadata['k1']);
        $this->assertSame(['v2'], $metadata['k2']);

        $server_call = $event->call;
        $server_call->startBatch([
            Grpc\OP_SEND_INITIAL_METADATA   => [],
            Grpc\OP_SEND_STATUS_FROM_SERVER => [
                'metadata' => [],
                'code'     => Grpc\STATUS_OK,
                'details'  => '',
            ],
            Grpc\OP_RECV_CLOSE_ON_SERVER    => true,
        ]);

        unset($call);
        unset($server_call);
    }
}
