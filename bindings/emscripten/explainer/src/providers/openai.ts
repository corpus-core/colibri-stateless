/**
 * Copyright (c) 2025 corpus.core
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

import type { LLMProvider, LLMProviderConfig } from '../types.js';

/**
 * OpenAI-compatible chat completions provider.
 * Works with OpenAI, Ollama (`/v1/chat/completions`), and any endpoint
 * that implements the OpenAI chat completions API.
 */
export class OpenAIProvider implements LLMProvider {
    private apiKey: string;
    private model: string;
    private baseUrl: string;
    private maxTokens: number;
    private temperature: number;

    constructor(config: LLMProviderConfig) {
        this.apiKey = config.apiKey || '';
        this.model = config.model || 'gpt-4o-mini';
        this.baseUrl = (config.baseUrl || 'https://api.openai.com').replace(/\/+$/, '');
        this.maxTokens = config.maxTokens || 1024;
        this.temperature = config.temperature ?? 0.2;
    }

    async complete(systemPrompt: string, userPrompt: string): Promise<string> {
        const url = `${this.baseUrl}/v1/chat/completions`;

        const headers: Record<string, string> = { 'Content-Type': 'application/json' };
        if (this.apiKey) {
            headers['Authorization'] = `Bearer ${this.apiKey}`;
        }

        const res = await fetch(url, {
            method: 'POST',
            headers,
            body: JSON.stringify({
                model: this.model,
                messages: [
                    { role: 'system', content: systemPrompt },
                    { role: 'user', content: userPrompt },
                ],
                max_tokens: this.maxTokens,
                temperature: this.temperature,
            }),
        });

        if (!res.ok) {
            const body = await res.text().catch(() => '');
            throw new Error(`OpenAI API error ${res.status}: ${body}`);
        }

        const json = await res.json() as {
            choices?: { message?: { content?: string } }[];
            error?: { message?: string };
        };

        if (json.error) {
            throw new Error(`OpenAI API error: ${json.error.message}`);
        }

        const content = json.choices?.[0]?.message?.content;
        if (!content) {
            throw new Error('OpenAI API returned empty response');
        }

        return content;
    }
}
