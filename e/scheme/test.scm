; não existe fold nem map no r5rs. ele está no SRFI-1 e r6rs

; http://srfi.schemers.org/srfi-1/srfi-1.html#FoldUnfoldMap

(define (fold f default ls)
  (if (null? ls)
      default
      (fold f (f (car ls) default) (cdr ls))))

; o meu map só funciona com funções de apenas um argumento

(define (map1 f ls)
  (if (null? ls)
      '()
      (cons (f (car ls)) (map1 f (cdr ls)))))

; Suponha que voce queira uma lista com todas as permutações
; da lista ls.

; Se ls for vazia, entao a resposta é uma lista vazia. Esse é 
; nosso "caso base".

; Agora vamos para o outro caso.

; Suponha que eu já sei que as permutações da lista (cdr ls)
; é X. (cdr ls) é ls menos o elemento do topo.

; Se eu sei X, basta construir uma função que pegue (car ls),
; o elemento do topo, e combine com X, de modo a ter como
; resultado as permutações de ls. Isso é muito comum quando
; se usa o fold.

; Suponha que eu já tenho essa função, e vamos chamar ela de
; proximo-passo. Então, poderiamos escrever 

(define (construir-permutacoes proximo-passo ls)
  (fold proximo-passo '() ls)

; Aí está a mágica do fold. É necessário algum tempo para
; absorvê-la.

; A função construir-permutacoes recebe como parâmetro a
; função proximo-passo porque, na verdade, eu ainda não
; escrevi ela :-) ela recebe também, claro, a lista ls como
; parâmetro. O resultado de construir-permutacoes vai ser uma
; lista com as permutações de ls (o que inclui o próprio ls)

; Okay, vamos para o proximo-passo. Pela semântica do fold,
; o primeiro parâmetro é um acumulador. Ele começa em '(), e
; a medida que o programa vai evoluindo, ele sempre carrega o
; valor das permutacoes anteriormente processada. O segundo
; parâmetro é o próximo valor a ser inserido: inicialmente é
; (car ls), depois (cadr ls), depois (caddr ls), etc, até
; exaurir a lista.

; O caso base, denovo, é a lista vazia. Se eu adicionar o
; valor v à uma lista vazia de permutações, o resultado vai
; ser uma lista com apenas uma permutação. Essa permutação
; será uma lista com apenas um elemento, v.

; Perceba que podemos obter diretamente esse resultado com
; (construir-permutacoes proximo-passo '() `(,v)), mas ele
; será um resultado intermediário ao construir permutações
; maiores. (Por que?)

; Ok, e no caso recursivo? Bom, basta eu adicionar v a cada
; elemento do acumulador, em todas as posições possíveis.

; Por exemplo, se o acumulador for ((a b) (b a)), eu teria
; que adicionar v em 3 posições de (a b), criando:

; (v a b)
; (a v b)
; (a b v)

; E adicionar v em 3 posições de (b a). O resultado seria

; ((v a b) (a v b) (a b v) (v b a) (b v a) (v a b))

; Imagine que eu tenho a função difícil: ela adiciona v em
; todas as posições de (a b), e retorna o resultado.
; Vou chamar essa função de expandir-elemento. Daí,
; proximo-passo poderia fazer isso:

; (map (lambda (x) expandir-elemento '(a b) x) 'v)

; Mas daí o resultado seria esse:

; (((v a b) (a v b) (a b v)) ((v b a) (b v a) (v a b)))

; Mas daí o meu resultado está separado em listas, cada uma
; contendo a expansão de um elemento de (a b). Mas eu posso
; juntar tudo com append. Daí eu tenho o resultado esperado.

; No fim, o proximo-passo pode ser definido assim:

(define (proximo-passo expandir-elemento acc v)
 append
             (map1
              (lambda (x)
                      expandir-elemento v x)
              acc))

; Uff! Falta definir expandir-elemento! O caso base, como
; sempre, é a lista vazia para o primeiro parâmetro. O
; resultado é ((v)), sendo v o segundo parâmetro (Por que?).

; Okay. E se 

(define (expandir-elemento v acc)
  (if (null? acc)
      `((,v))
      

(a | b c)

(v a b c)

(car v acc)


