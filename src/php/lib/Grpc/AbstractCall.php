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

namespace Grpc;

/**
 * Class AbstractCall.
 * @package Grpc
 */
abstract class AbstractCall
{
    /**
     * @var Call
     */
    protected $call;
    protected $deserialize;
    protected $metadata;
    protected $trailing_metadata;

    private $call_credentials_callback = null;
    private $method;
    private $service_url;

    private static $pure_call_credentials_enabled = null;

    private static function isPureCallCredentialsEnabled(): bool
    {
        if (self::$pure_call_credentials_enabled === null) {
            self::$pure_call_credentials_enabled =
                (bool)getenv('GRPC_PHP_PURE_CALL_CREDENTIALS');
        }
        return self::$pure_call_credentials_enabled;
    }

    /**
     * Create a new Call wrapper object.
     *
     * @param Channel  $channel     The channel to communicate on
     * @param string   $method      The method to call on the
     *                              remote server
     * @param callback $deserialize A callback function to deserialize
     *                              the response
     * @param array    $options     Call options (optional)
     */
    public function __construct(Channel $channel,
                                $method,
                                $deserialize,
                                array $options = [])
    {
        if (array_key_exists('timeout', $options) &&
            is_numeric($timeout = $options['timeout'])
        ) {
            $now = Timeval::now();
            $delta = new Timeval($timeout);
            $deadline = $now->add($delta);
        } else {
            $deadline = Timeval::infFuture();
        }
        $this->call = new Call($channel, $method, $deadline);
        $this->deserialize = $deserialize;
        $this->metadata = null;
        $this->trailing_metadata = null;
        $this->method = $method;
        // _grpc_service_url is an internal key set by BaseStub before call
        // construction so that the hostname_override-aware JWT audience URL is
        // available here without re-deriving it. Falls back to _buildServiceUrl
        // when the call is constructed directly (not via BaseStub).
        $this->service_url = $options['_grpc_service_url']
            ?? $this->_buildServiceUrl($channel, $method);
        if (array_key_exists('call_credentials_callback', $options) &&
            is_callable($options['call_credentials_callback'])
        ) {
            if (self::isPureCallCredentialsEnabled()) {
                $this->call_credentials_callback =
                    $options['call_credentials_callback'];
            } else {
                $call_credentials = CallCredentials::createFromPlugin(
                    $options['call_credentials_callback']
                );
                $this->call->setCredentials($call_credentials);
            }
        }
    }

    /**
     * Build a service URL from a channel target and method name, mirroring
     * the grpc_auth_metadata_context.service_url field that gRPC core populates
     * for plugin credentials.
     *
     * Only port 443 is stripped, matching gRPC core behaviour:
     * https://github.com/grpc/grpc/blob/master/src/core/filter/auth/client_auth_filter.cc
     *
     * Note: this fallback cannot apply hostname_override. When called via
     * BaseStub the correct URL is injected as _grpc_service_url instead.
     */
    private function _buildServiceUrl(Channel $channel, string $method): string
    {
        $last_slash = strrpos($method, '/');
        $service = $last_slash !== false
            ? substr($method, 0, $last_slash)
            : $method;
        $target = $channel->getTarget();
        if (strlen($target) > 4 && substr($target, -4) === ':443') {
            $target = substr($target, 0, -4);
        }
        return 'https://' . $target . $service;
    }

    /**
     * Apply call credentials callback (pure-PHP path) to the outgoing metadata.
     * Returns $metadata unchanged when no callback is set or the experiment is off.
     *
     * @param array $metadata Outgoing metadata
     * @return array Metadata merged with credentials callback output
     * @throws \InvalidArgumentException if the callback returns a non-array or
     *                                   a metadata array with invalid keys
     */
    protected function _applyCallCredentials(array $metadata): array
    {
        if ($this->call_credentials_callback === null) {
            return $metadata;
        }
        $context = (object)[
            'service_url' => $this->service_url,
            'method_name' => $this->method,
        ];
        $extra = ($this->call_credentials_callback)($context);
        if (!is_array($extra)) {
            throw new \InvalidArgumentException(
                'Call credentials callback must return an array of metadata'
            );
        }
        $normalized = [];
        foreach ($extra as $key => $value) {
            $key = strtolower((string)$key);
            // Dots are permitted by the HTTP/2 HPACK spec for header names.
            if (!preg_match('/^[.a-z\d_-]+$/', $key)) {
                throw new \InvalidArgumentException(
                    'Metadata keys returned by call credentials callback must be ' .
                    'lowercase alphanumeric, hyphens, underscores, or dots'
                );
            }
            $normalized[$key] = $value;
        }
        return array_merge_recursive($metadata, $normalized);
    }

    /**
     * @return mixed The metadata sent by the server
     */
    public function getMetadata()
    {
        return $this->metadata;
    }

    /**
     * @return mixed The trailing metadata sent by the server
     */
    public function getTrailingMetadata()
    {
        return $this->trailing_metadata;
    }

    /**
     * @return string The URI of the endpoint
     */
    public function getPeer()
    {
        return $this->call->getPeer();
    }

    /**
     * Cancels the call.
     */
    public function cancel()
    {
        $this->call->cancel();
    }

    /**
     * Serialize a message to the protobuf binary format.
     *
     * @param mixed $data The Protobuf message
     *
     * @return string The protobuf binary format
     */
    protected function _serializeMessage($data)
    {
        // Proto3 implementation
        return $data->serializeToString();
    }

    /**
     * Deserialize a response value to an object.
     *
     * @param string $value The binary value to deserialize
     *
     * @return mixed The deserialized value
     */
    protected function _deserializeResponse($value)
    {
        if ($value === null) {
            return;
        }
        list($className, $deserializeFunc) = $this->deserialize;
        $obj = new $className();
        $obj->mergeFromString($value);
        return $obj;
    }

    /**
     * Set the CallCredentials for the underlying Call.
     *
     * Pure-PHP path (GRPC_PHP_PURE_CALL_CREDENTIALS=1): accepts a callable
     * and stores it for application at start() time.
     *
     * Legacy path: accepts a CallCredentials object and delegates to the C
     * extension as before.
     *
     * @param CallCredentials|callable $call_credentials
     */
    public function setCallCredentials($call_credentials)
    {
        if (self::isPureCallCredentialsEnabled() && is_callable($call_credentials)) {
            $this->call_credentials_callback = $call_credentials;
        } else {
            $this->call->setCredentials($call_credentials);
        }
    }
}
