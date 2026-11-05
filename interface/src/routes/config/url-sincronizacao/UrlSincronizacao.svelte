<script lang="ts">
	import Link from '~icons/tabler/link';
	import Server from '~icons/tabler/server';
	import Check from '~icons/tabler/check';
	import AlertCircle from '~icons/tabler/alert-circle';
	import Refresh from '~icons/tabler/refresh';
	import SettingsCard from '$lib/components/SettingsCard.svelte';
	import { notifications } from '$lib/components/toasts/notifications';
	import {
		DEFAULT_SYNC_URL,
		SYNC_URL_STORAGE_KEY,
		getSyncStatusRemoto,
		resolverEndpointsSync
	} from '$lib/services/services';
	import type { SyncStatusResponse } from '$lib/types/models';
	import { onMount } from 'svelte';

	let syncUrl = $state(DEFAULT_SYNC_URL);
	let testando = $state(false);
	let statusConexao = $state<'idle' | 'sucesso' | 'erro'>('idle');
	let mensagemStatus = $state('');
	let infoEstacaoRemota = $state<SyncStatusResponse | null>(null);
	let rttMs = $state<number | null>(null);

	let endpointsResolvidos = $derived.by(() => {
		try {
			return resolverEndpointsSync(syncUrl);
		} catch {
			return null;
		}
	});

	onMount(() => {
		try {
			const savedUrl = localStorage.getItem(SYNC_URL_STORAGE_KEY);
			if (savedUrl && savedUrl.trim() !== '') {
				syncUrl = savedUrl;
			}
		} catch (e) {
			console.warn('Não foi possível acessar localStorage:', e);
		}
	});

	function validarUrl(url: string): boolean {
		try {
			const parsed = new URL(url.trim());
			return parsed.protocol === 'http:' || parsed.protocol === 'https:';
		} catch {
			return false;
		}
	}

	async function testarConexao() {
		if (!validarUrl(syncUrl)) {
			notifications.error('URL inválida. Deve iniciar com http:// ou https://', 3000);
			statusConexao = 'erro';
			mensagemStatus = 'URL inválida.';
			infoEstacaoRemota = null;
			return;
		}

		testando = true;
		statusConexao = 'idle';
		mensagemStatus = 'Testando conectividade HTTP real com a estação alvo...';
		infoEstacaoRemota = null;
		rttMs = null;

		try {
			const resultado = await getSyncStatusRemoto(syncUrl, 5000);
			statusConexao = 'sucesso';
			infoEstacaoRemota = resultado.status;
			rttMs = resultado.rttMs;
			mensagemStatus = `Estação online confirmada (RTT: ${resultado.rttMs} ms) — Relógio remoto: ${resultado.status.relogio || 'N/D'}`;
			notifications.success('Conexão HTTP verificada com sucesso na estação alvo!', 3000);
		} catch (error: any) {
			statusConexao = 'erro';
			const detalhe = error?.message || 'Verifique o endereço IP e a conexão Wi-Fi.';
			mensagemStatus = `Falha ao conectar com a estação remota: ${detalhe}`;
			notifications.error(`Estação inalcançável: ${detalhe}`, 4000);
		} finally {
			testando = false;
		}
	}

	function salvar() {
		if (!validarUrl(syncUrl)) {
			notifications.error('URL inválida. Verifique o formato inserido.', 3000);
			return;
		}

		try {
			const normalizada = endpointsResolvidos ? endpointsResolvidos.origin : syncUrl.trim();
			syncUrl = normalizada;
			localStorage.setItem(SYNC_URL_STORAGE_KEY, normalizada);
			notifications.success('Endereço da estação de sincronização salvo com sucesso!', 3000);
		} catch (e) {
			console.error('Erro ao salvar no localStorage:', e);
			notifications.error('Erro ao salvar configuração no navegador.', 3000);
		}
	}

	function restaurarPadrao() {
		syncUrl = DEFAULT_SYNC_URL;
		salvar();
		statusConexao = 'idle';
		mensagemStatus = '';
		infoEstacaoRemota = null;
		rttMs = null;
		notifications.info('Endereço restaurado para o padrão (http://192.168.4.1).', 3000);
	}
</script>

<SettingsCard collapsible={false} maxWidth="max-w-3xl">
	{#snippet icon()}
		<Link class="mr-2 inline h-6 w-6 text-primary shrink-0" />
	{/snippet}
	{#snippet title()}
		<span class="text-lg sm:text-xl font-bold">Endereço de Sincronização</span>
	{/snippet}

	<div class="space-y-4 sm:space-y-6 pt-1 w-full min-w-0">
		<div class="bg-base-100 p-4 sm:p-6 rounded-box border border-base-200 shadow-sm w-full">
			<div class="flex items-center space-x-3 mb-4 pb-3 border-b border-base-200">
				<div class="p-2 bg-primary/10 text-primary rounded-lg shrink-0">
					<Link class="h-5 w-5 sm:h-6 sm:w-6" />
				</div>
				<div>
					<h2 class="text-base sm:text-lg font-bold text-base-content">Endereço da Estação Central / Alvo</h2>
					<p class="text-xs text-base-content/60">
						Defina o IP da outra estação para sincronização inicial, ajuste de relógio e consolidação final.
					</p>
				</div>
			</div>

			<div class="mb-4">
				<label for="syncUrl" class="block mb-1.5 text-xs sm:text-sm font-semibold text-base-content">
					URL ou IP da Estação Alvo
				</label>
				<input
					id="syncUrl"
					type="url"
					bind:value={syncUrl}
					disabled={testando}
					placeholder="http://192.168.4.1"
					class="input input-bordered input-sm sm:input-md w-full font-mono text-sm"
				/>
				<p class="text-xs text-base-content/60 mt-1.5">
					Exemplos: <code class="font-mono text-primary font-bold">http://192.168.4.1</code> (AP padrão ESP32) ou <code class="font-mono text-primary font-bold">http://192.168.0.100</code> (rede Wi-Fi local).
				</p>

				{#if endpointsResolvidos}
					<div class="mt-3 p-3 bg-base-200/60 rounded-box border border-base-300 text-xs space-y-1 font-mono text-base-content/80">
						<div class="truncate"><strong class="font-sans text-base-content">Status/Relógio:</strong> {endpointsResolvidos.statusUrl}</div>
						<div class="truncate"><strong class="font-sans text-base-content">Sync Inicial:</strong> {endpointsResolvidos.configUrl}</div>
						<div class="truncate"><strong class="font-sans text-base-content">Sync Final:</strong> {endpointsResolvidos.resultadosUrl}</div>
					</div>
				{/if}
			</div>

			{#if statusConexao !== 'idle'}
				<div
					class="p-3 mb-4 rounded-box flex items-start space-x-2 text-xs sm:text-sm {statusConexao === 'sucesso'
						? 'bg-success/15 text-success-content border border-success/30'
						: 'bg-warning/15 text-warning-content border border-warning/30'}"
				>
					{#if statusConexao === 'sucesso'}
						<Check class="h-5 w-5 text-success shrink-0 mt-0.5" />
					{:else}
						<AlertCircle class="h-5 w-5 text-warning shrink-0 mt-0.5" />
					{/if}
					<div class="space-y-1 min-w-0">
						<div class="font-medium">{mensagemStatus}</div>
						{#if infoEstacaoRemota}
							<div class="text-xs flex flex-wrap gap-x-3 gap-y-1 pt-1 opacity-90">
								<span>Prova: <strong>{infoEstacaoRemota.prova || 'Não configurada'}</strong></span>
								<span>Atletas: <strong>{infoEstacaoRemota.atletas ?? 0}</strong></span>
								<span>Cartões RFID: <strong>{infoEstacaoRemota.cartoes ?? 0}</strong></span>
								<span>Registros: <strong>{infoEstacaoRemota.registros ?? 0}</strong></span>
								{#if rttMs !== null}
									<span>Latência: <strong>{rttMs} ms</strong></span>
								{/if}
							</div>
						{/if}
					</div>
				</div>
			{/if}

			<div class="flex flex-wrap items-center gap-2 pt-3 border-t border-base-200">
				<button
					type="button"
					onclick={salvar}
					disabled={testando}
					class="btn btn-primary btn-sm sm:btn-md gap-1.5"
				>
					<Server class="h-4 w-4" />
					<span>Salvar Configuração</span>
				</button>

				<button
					type="button"
					onclick={testarConexao}
					disabled={testando}
					class="btn btn-outline btn-sm sm:btn-md gap-1.5"
				>
					<Refresh class="h-4 w-4 {testando ? 'animate-spin' : ''}" />
					<span>{testando ? 'Testando...' : 'Testar Conexão'}</span>
				</button>

				<button
					type="button"
					onclick={restaurarPadrao}
					disabled={testando}
					class="btn btn-ghost btn-sm sm:btn-md ml-auto"
				>
					Restaurar Padrão
				</button>
			</div>
		</div>

		<div class="bg-base-100 p-4 sm:p-6 rounded-box border border-base-200 shadow-sm w-full">
			<h3 class="text-sm sm:text-base font-bold text-base-content mb-2">Protocolo de Sincronização entre Estações</h3>
			<p class="text-xs text-base-content/70 mb-3">
				O firmware ESP32 expõe endpoints dedicados para replicação de cadastros iniciais, sincronismo de relógio RTC DS3231 e conciliação idempotente de resultados:
			</p>
			<pre class="bg-neutral text-neutral-content p-3 rounded-box text-xs overflow-x-auto font-mono"><code>GET/POST /rest/sync/config      -&gt; Prova, Categorias, Pilotos, Cartões RFID e Relógio RTC
GET/POST /rest/sync/resultados  -&gt; Conciliação idempotente (Largada + Chegada) e conflitos
GET      /rest/sync/status      -&gt; Diagnóstico da estação e medição de latência (RTT)
POST     /rest/estacao/reset    -&gt; Reset completo ou seletivo para início de nova prova</code></pre>
		</div>
	</div>
</SettingsCard>
